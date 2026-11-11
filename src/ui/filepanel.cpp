#include "ui/filepanel.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QStyle>
#include <QStyleFactory>
#include <QTableView>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include "core/filemask.h"
#include "core/localfilesource.h"
#include "core/searchresultfilesource.h"
#include "ui/filelistmodel.h"

namespace dcpp {

namespace {

constexpr int RefreshDelayMs = 250;
constexpr int MaxRedirects = 3;

#if defined(Q_OS_WIN)
// Re-reads the drive list each time it opens, so new USB drives show up.
class DriveComboBox : public QComboBox
{
public:
    using QComboBox::QComboBox;

    void refresh()
    {
        const QString current = currentData().toString();
        clear();
        for (const QFileInfo &drive : QDir::drives()) {
            const QString root = drive.absoluteFilePath();
            addItem(QDir::toNativeSeparators(root).left(2), root);
        }
        const int i = findData(current);
        setCurrentIndex(i);
    }

    void showPopup() override
    {
        refresh();
        QComboBox::showPopup();
    }
};
#endif

} // namespace

FilePanel::FilePanel(std::shared_ptr<const FileSource> source, QWidget *parent)
    : QWidget(parent)
    , m_local(std::move(source))
    , m_source(m_local)
    , m_loadingSource(m_local)
{
    m_model = new FileListModel(this);

    m_view = new QTableView(this);
    m_view->setModel(m_model);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setTabKeyNavigation(false);
    m_view->setShowGrid(false);
    m_view->setWordWrap(false);
    m_view->verticalHeader()->hide();
    m_view->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 4);
    // Fusion draws one continuous row highlight. The Windows 11 style marks each cell separately.
    QStyle *viewStyle = QStyleFactory::create(QStringLiteral("Fusion"));
    if (viewStyle != nullptr) {
        viewStyle->setParent(m_view);
        m_view->setStyle(viewStyle);
    }
    m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->horizontalHeader()->resizeSection(static_cast<int>(SortColumn::Name), 220);
    m_view->horizontalHeader()->setHighlightSections(false);
    m_view->horizontalHeader()->setSortIndicator(static_cast<int>(SortColumn::Name), Qt::AscendingOrder);
    m_view->setSortingEnabled(true);
    m_view->installEventFilter(this);

    m_pathEdit = new QLineEdit(this);
    m_pathEdit->installEventFilter(this);
    m_status = new QLabel(this);

    auto *top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
#if defined(Q_OS_WIN)
    auto *drives = new DriveComboBox(this);
    drives->refresh();
    m_drives = drives;
    top->addWidget(m_drives);
    connect(m_drives, &QComboBox::activated, this, [this](int index) {
        const QString root = m_drives->itemData(index).toString();
        if (!root.isEmpty()) {
            setPath(root);
            focusView();
        }
    });
#endif
    top->addWidget(m_pathEdit, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addLayout(top);
    layout->addWidget(m_view, 1);
    layout->addWidget(m_status);

    connect(&m_loader, &QFutureWatcher<ListResult>::finished, this, &FilePanel::onLoadFinished);
    connect(m_view, &QTableView::doubleClicked, this, [this] { openCurrent(); });
    connect(m_pathEdit, &QLineEdit::returnPressed, this, [this] {
        setPath(LocalFileSource::normalise(m_pathEdit->text()));
        focusView();
    });
    connect(m_model, &FileListModel::marksChanged, this, &FilePanel::updateStatus);
    connect(m_model, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        m_cursorBeforeReset = currentName();
    });
    connect(m_model, &QAbstractItemModel::modelReset, this, [this] {
        const QString kept = m_keepCursor ? m_cursorBeforeReset : QString();
        const QString target = m_selectAfterLoad.isEmpty() ? kept : m_selectAfterLoad;
        m_selectAfterLoad.clear();
        selectName(target);
    });

    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(RefreshDelayMs);
    connect(&m_refreshTimer, &QTimer::timeout, this, &FilePanel::reload);
    connect(&m_fsWatcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_refreshTimer.start(); });

    setActive(false);
}

QString FilePanel::path() const
{
    return m_path;
}

void FilePanel::setPath(const QString &path, const QString &selectName)
{
    loadFrom(m_local, path, selectName);
}

void FilePanel::showSearchResults(std::shared_ptr<const SearchResultFileSource> results)
{
    m_results = results;
    loadFrom(std::move(results), SearchResultFileSource::rootPath(), QString());
}

bool FilePanel::isShowingResults() const
{
    return m_source != m_local;
}

QString FilePanel::persistentPath() const
{
    return isShowingResults() && m_results ? m_results->baseDir() : m_path;
}

QString FilePanel::currentPath() const
{
    const QString name = currentName();
    return name.isEmpty() ? QString() : m_source->childPath(m_path, name);
}

void FilePanel::loadFrom(std::shared_ptr<const FileSource> source, const QString &path, const QString &selectName)
{
    m_loadingSource = source;
    m_loadingPath = path;
    m_selectAfterLoad = selectName;
    m_status->setText(tr("Loading…"));
    // setFuture detaches the previous load, so a slow earlier listing can never overwrite this one.
    m_loader.setFuture(QtConcurrent::run([source, path] { return source->list(path); }));
}

void FilePanel::reload()
{
    if (!m_path.isEmpty()) {
        loadFrom(m_source, m_path, currentName());
    }
}

void FilePanel::onLoadFinished()
{
    const ListResult result = m_loader.result();
    const QString loaded = m_loadingPath;
    const bool local = m_loadingSource == m_local;

    if (!result.ok) {
        // The current folder vanished: fall back to the closest folder that still exists.
        if (local && loaded == m_path && !QFileInfo(m_path).isDir() && m_redirects < MaxRedirects) {
            ++m_redirects;
            setPath(LocalFileSource::nearestExistingDir(m_path));
            return;
        }
        if (m_path.isEmpty() && m_redirects < MaxRedirects) {
            ++m_redirects;
            setPath(LocalFileSource::nearestExistingDir(QDir::homePath()));
            return;
        }
        m_selectAfterLoad.clear();
        m_pathEdit->setText(m_source->displayPath(m_path));
        showError(result.error);
        return;
    }

    m_redirects = 0;
    const bool sameDir = loaded == m_path && m_loadingSource == m_source;
    if (!sameDir) {
        if (!m_fsWatcher.directories().isEmpty()) {
            m_fsWatcher.removePaths(m_fsWatcher.directories());
        }
        if (local) {
            // Without a watch the panel still works, it just needs a manual refresh.
            (void)m_fsWatcher.addPath(loaded);
        }
    }
    m_source = m_loadingSource;
    if (local) {
        m_results.reset();
    }
    m_path = loaded;
    m_pathEdit->setText(m_source->displayPath(m_path));
    updateDriveCombo();
    if (!sameDir) {
        emit pathChanged(m_path);
    }

    const bool showParent = !m_source->parentPath(m_path).isEmpty();
    m_keepCursor = sameDir;
    m_model->setItems(result.items, showParent, sameDir);
    m_keepCursor = true;
}

QStringList FilePanel::selectedPaths() const
{
    QStringList paths;
    const QStringList marked = m_model->markedNames();
    if (!marked.isEmpty()) {
        for (const QString &name : marked) {
            paths.append(m_source->childPath(m_path, name));
        }
        return paths;
    }
    const QString name = currentName();
    if (!name.isEmpty()) {
        paths.append(m_source->childPath(m_path, name));
    }
    return paths;
}

QString FilePanel::currentName() const
{
    const FileItem *item = m_model->itemAt(m_view->currentIndex().row());
    return item == nullptr ? QString() : item->name;
}

void FilePanel::setActive(bool active)
{
    m_active = active;
    m_pathEdit->setStyleSheet(active
        ? QStringLiteral("QLineEdit { background: palette(highlight); color: palette(highlighted-text); }")
        : QString());
}

void FilePanel::focusView()
{
    m_view->setFocus();
}

SortColumn FilePanel::sortColumn() const
{
    return m_model->sortColumn();
}

Qt::SortOrder FilePanel::sortOrder() const
{
    return m_model->sortOrder();
}

void FilePanel::setSort(SortColumn column, Qt::SortOrder order)
{
    m_view->horizontalHeader()->setSortIndicator(static_cast<int>(column), order);
}

bool FilePanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FocusIn) {
        emit activated(this);
        return QWidget::eventFilter(watched, event);
    }
    if (watched != m_view || event->type() != QEvent::KeyPress) {
        return QWidget::eventFilter(watched, event);
    }

    const auto *key = static_cast<QKeyEvent *>(event);
    const Qt::KeyboardModifiers mods = key->modifiers() & ~Qt::KeypadModifier;
    switch (key->key()) {
    case Qt::Key_Tab:
        if (mods == Qt::NoModifier) {
            emit switchPanelRequested();
            return true;
        }
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (mods == Qt::NoModifier) {
            openCurrent();
            return true;
        }
        break;
    case Qt::Key_Backspace:
        if (mods == Qt::NoModifier) {
            goParent();
            return true;
        }
        break;
    case Qt::Key_PageUp:
        if (mods == Qt::ControlModifier) {
            goParent();
            return true;
        }
        break;
    case Qt::Key_Insert:
    case Qt::Key_Space:
        if (mods == Qt::NoModifier) {
            markAndMoveDown();
            return true;
        }
        break;
    case Qt::Key_Plus:
        markByMaskPrompt(true);
        return true;
    case Qt::Key_Minus:
        markByMaskPrompt(false);
        return true;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void FilePanel::openCurrent()
{
    const int row = m_view->currentIndex().row();
    if (m_model->isParentRow(row)) {
        goParent();
        return;
    }
    const FileItem *item = m_model->itemAt(row);
    if (item == nullptr) {
        return;
    }
    const QString target = m_source->childPath(m_path, item->name);
    if (item->isDir) {
        setPath(target);
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(target))) {
        showError(tr("No application is associated with %1.").arg(item->name));
    }
}

void FilePanel::goParent()
{
    if (isShowingResults() && m_results) {
        setPath(m_results->baseDir());
        return;
    }
    const QString parent = m_source->parentPath(m_path);
    if (!parent.isEmpty()) {
        setPath(parent, QDir(m_path).dirName());
    }
}

void FilePanel::markAndMoveDown()
{
    const QModelIndex current = m_view->currentIndex();
    if (!current.isValid()) {
        return;
    }
    m_model->toggleMark(current.row());
    const int next = std::min(current.row() + 1, m_model->rowCount() - 1);
    m_view->setCurrentIndex(m_model->index(next, 0));
}

void FilePanel::markByMaskPrompt(bool mark)
{
    bool ok = false;
    const QString text = QInputDialog::getText(this, mark ? tr("Select files") : tr("Unselect files"),
                                               tr("Mask (separate several with ;):"),
                                               QLineEdit::Normal, QStringLiteral("*.*"), &ok);
    if (!ok) {
        return;
    }
    const FileMask mask(text);
    if (!mask.isValid()) {
        showError(tr("Invalid mask: %1").arg(text));
        return;
    }
    m_model->markByMask(mask, mark);
}

void FilePanel::selectName(const QString &name)
{
    int row = name.isEmpty() ? -1 : m_model->rowOfName(name);
    if (row < 0) {
        row = 0;
    }
    if (row < m_model->rowCount()) {
        const QModelIndex index = m_model->index(row, 0);
        m_view->setCurrentIndex(index);
        m_view->scrollTo(index);
    }
    updateStatus();
}

void FilePanel::updateStatus()
{
    const QLocale locale;
    m_status->setText(tr("%1 of %2 marked, %3 of %4")
                          .arg(m_model->markedCount())
                          .arg(m_model->fileCount())
                          .arg(locale.formattedDataSize(m_model->markedSize()),
                               locale.formattedDataSize(m_model->totalSize())));
}

void FilePanel::updateDriveCombo()
{
    if (m_drives == nullptr) {
        return;
    }
    for (int i = 0; i < m_drives->count(); ++i) {
        const QString root = m_drives->itemData(i).toString();
        if (m_path.startsWith(root, Qt::CaseInsensitive) || m_path + QLatin1Char('/') == root) {
            m_drives->setCurrentIndex(i);
            return;
        }
    }
    m_drives->setCurrentIndex(-1);
}

void FilePanel::showError(const QString &message)
{
    m_status->setText(message);
}

} // namespace dcpp
