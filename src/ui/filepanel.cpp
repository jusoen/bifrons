#include "ui/filepanel.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QContextMenuEvent>
#include <QDrag>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QStyle>
#include <QStyleFactory>
#include <QTableView>
#include <QUrl>
#include <QVBoxLayout>
#include <QPointer>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include "core/filemask.h"
#include "core/transfer.h"
#include "core/localfilesource.h"
#include "core/searchresultfilesource.h"
#include "ui/fileicons.h"
#include "ui/filelistmodel.h"
#include "ui/pathbar.h"
#include "ui/terminal.h"

namespace bifrons {

namespace {

constexpr int RefreshDelayMs = 250;
constexpr int MaxRedirects = 3;
constexpr int MaxHistory = 50;
constexpr int ConnectingDelayMs = 300;
// Folder size counts share a small pool so they never starve folder listing.
constexpr int FolderSizeThreads = 2;

// Direction-free drag cursor in the Explorer style: a pointer with a small box, plus a "+" for copy.
QPixmap dragCursor(bool copy, qreal dpr)
{
    constexpr int Size = 26;
    QPixmap pixmap(QSize(Size, Size) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    // The tip is at (0, 0), where Qt places the cursor hotspot.
    const QPolygonF pointer({QPointF(0.5, 0.5), QPointF(0.5, 16), QPointF(4.5, 12.5), QPointF(7.5, 18.5),
                             QPointF(9.5, 17.5), QPointF(6.8, 11.5), QPointF(11.5, 11.5)});
    painter.setPen(QPen(Qt::black, 1));
    painter.setBrush(Qt::white);
    painter.drawPolygon(pointer);
    const QRectF badge(12.5, 14.5, 11, 11);
    painter.setBrush(Qt::white);
    painter.drawRect(badge);
    if (copy) {
        painter.setPen(QPen(Qt::black, 1.6));
        const QPointF c = badge.center();
        painter.drawLine(QPointF(c.x() - 3, c.y()), QPointF(c.x() + 3, c.y()));
        painter.drawLine(QPointF(c.x(), c.y() - 3), QPointF(c.x(), c.y() + 3));
    }
    painter.end();
    return pixmap;
}

QThreadPool &folderSizePool()
{
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool(qApp);
        p->setMaxThreadCount(FolderSizeThreads);
        return p;
    }();
    return *pool;
}

// Total size of the files below dir. Symlinks and junctions are not followed, so a junction that
// points back up the tree (as in Windows user profiles) cannot loop. Returns -1 when cancelled.
qint64 folderSize(const QString &dir, const std::atomic<bool> &cancel)
{
    qint64 total = 0;
    QStringList pending{dir};
    // Bounded by the tree: each folder is visited once because links are never followed.
    while (!pending.isEmpty()) {
        if (cancel.load()) {
            return -1;
        }
        const QFileInfoList entries = QDir(pending.takeLast())
                                          .entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden
                                                             | QDir::System,
                                                         QDir::NoSort);
        for (const QFileInfo &info : entries) {
            if (info.isSymLink() || info.isJunction()) {
                continue;
            }
            if (info.isDir()) {
                pending.append(info.absoluteFilePath());
            } else {
                total += info.size();
            }
        }
    }
    return total;
}

} // namespace

FilePanel::FilePanel(std::shared_ptr<const FileSource> source, QWidget *parent)
    : QWidget(parent)
    , m_local(std::move(source))
    , m_source(m_local)
    , m_loadingSource(m_local)
{
    m_model = new FileListModel(this);
    m_model->setIconLookup([this](const FileItem &item) {
        return FileIcons::instance().icon(item, m_source->childPath(m_path, item.name));
    });
    connect(&FileIcons::instance(), &FileIcons::iconReady, this, [this](const QString &path) {
        // Only rows of the folder on show. A search result's name may include a subfolder.
        const QString base = isShowingResults() && m_results ? m_results->baseDir() : m_path;
        const QString prefix = base.endsWith(QLatin1Char('/')) ? base : base + QLatin1Char('/');
        if (path.startsWith(prefix, Qt::CaseInsensitive)) {
            m_model->refreshIcon(path.mid(prefix.size()));
        }
    });

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
    m_view->viewport()->installEventFilter(this);
    m_view->viewport()->setAcceptDrops(true);

    m_pathBar = new PathBar(this);
    m_pathBar->lineEdit()->installEventFilter(this);
    m_status = new QLabel(this);
    // Long status text is clipped rather than widening the pane.
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Filter (text, or a mask such as *.cpp)"));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->installEventFilter(this);
    m_filterEdit->hide();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(m_pathBar);
    layout->addWidget(m_view, 1);
    layout->addWidget(m_filterEdit);
    layout->addWidget(m_status);
    connect(m_filterEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_model->setNameFilter(text);
    });

    connect(&m_loader, &QFutureWatcher<ListResult>::finished, this, &FilePanel::onLoadFinished);
    connect(m_view, &QTableView::doubleClicked, this, [this] { openCurrent(); });
    connect(m_pathBar, &PathBar::pathRequested, this, [this](const QString &path) {
        setPath(LocalFileSource::normalise(path));
        focusView();
    });
    connect(m_pathBar, &PathBar::editingCancelled, this, &FilePanel::focusView);
    connect(m_model, &FileListModel::marksChanged, this, &FilePanel::updateStatus);
    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this, &FilePanel::currentItemChanged);
    connect(m_model, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        cancelInlineRename();
        m_cursorBeforeReset = currentName();
    });
    connect(m_model, &QAbstractItemModel::modelReset, this, [this] {
        const QString kept = m_keepCursor ? m_cursorBeforeReset : QString();
        const QString target = m_selectAfterLoad.isEmpty() ? kept : m_selectAfterLoad;
        m_selectAfterLoad.clear();
        selectName(target);
    });

    m_message = new QLabel(m_view->viewport());
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    m_message->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_message->setStyleSheet(QStringLiteral("QLabel { color: palette(placeholder-text); }"));
    m_message->hide();
    auto *messageLayout = new QVBoxLayout(m_view->viewport());
    messageLayout->addWidget(m_message);

    m_connectingTimer.setSingleShot(true);
    m_connectingTimer.setInterval(ConnectingDelayMs);
    connect(&m_connectingTimer, &QTimer::timeout, this, [this] {
        showMessage(tr("Connecting to network…\n%1").arg(m_local->displayPath(m_loadingPath)));
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

std::shared_ptr<const SearchResultFileSource> FilePanel::searchResults() const
{
    return isShowingResults() ? m_results : nullptr;
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

QStringList FilePanel::filePaths() const
{
    QStringList paths;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const FileItem *item = m_model->itemAt(row);
        if (item != nullptr && !item->isDir) {
            paths.append(m_source->childPath(m_path, item->name));
        }
    }
    return paths;
}

void FilePanel::loadFrom(std::shared_ptr<const FileSource> source, const QString &path, const QString &selectName)
{
    m_loadingSource = source;
    m_loadingPath = path;
    m_historyTarget = -1;
    m_selectAfterLoad = selectName;
    m_status->setText(tr("Loading…"));
    m_networkPending = source == m_local && LocalFileSource::isNetworkPath(path);
    if (m_networkPending) {
        m_connectingTimer.start();
    } else {
        m_connectingTimer.stop();
    }
    emit navigationChanged();
    // setFuture detaches the previous load, so a slow earlier listing can never overwrite this one.
    m_loader.setFuture(QtConcurrent::run([source, path] { return source->list(path); }));
}

void FilePanel::restorePath(const QString &path)
{
    // The existence check for a network path can block for the SMB timeout, so the listing thread does it.
    if (LocalFileSource::isNetworkPath(path)) {
        // Show the folder in the tab and path bar at once. The listing confirms or fails it later.
        const QString target = LocalFileSource::normalise(path);
        m_path = target;
        recordHistory(target);
        updatePathBar();
        emit pathChanged(target);
        emit navigationChanged();
        loadFrom(m_local, target, QString());
    } else {
        setPath(LocalFileSource::nearestExistingDir(path));
    }
}

void FilePanel::showMessage(const QString &text)
{
    m_message->setText(text);
    m_message->setVisible(!text.isEmpty());
}

void FilePanel::reload()
{
    if (!m_path.isEmpty()) {
        const QString select = m_renamedTo.isEmpty() ? currentName() : m_renamedTo;
        m_renamedTo.clear();
        loadFrom(m_source, m_path, select);
    }
}

void FilePanel::onLoadFinished()
{
    const ListResult result = m_loader.result();
    const QString loaded = m_loadingPath;
    const bool local = m_loadingSource == m_local;
    m_connectingTimer.stop();
    m_networkPending = false;

    if (!result.ok) {
        // Stay on an unreachable network folder so it is saved and Refresh can retry it.
        // Checking it with QFileInfo here would block the UI for the SMB timeout.
        if (local && LocalFileSource::isNetworkPath(loaded)) {
            m_redirects = 0;
            m_selectAfterLoad.clear();
            m_unreachable = true;
            enterLocation(loaded, FileItemList(), false);
            showMessage(tr("Network connection failed\n%1\n\nPress Ctrl+R to retry.").arg(m_local->displayPath(loaded)));
            showError(result.error);
            return;
        }
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
        m_historyTarget = -1;
        updatePathBar();
        showError(result.error);
        emit navigationChanged();
        return;
    }

    m_redirects = 0;
    m_unreachable = false;
    showMessage(QString());
    enterLocation(loaded, result.items, true);
}

void FilePanel::enterLocation(const QString &loaded, const FileItemList &items, bool reachable)
{
    const bool local = m_loadingSource == m_local;
    const bool sameDir = loaded == m_path && m_loadingSource == m_source;
    const QStringList watched = m_fsWatcher.directories();
    const bool watching = watched.contains(loaded);
    if (!watched.isEmpty() && (!reachable || !watching)) {
        m_fsWatcher.removePaths(watched);
    }
    // Not watched when unreachable: adding the watch would wait on the network on this thread.
    if (local && reachable && !watching) {
        // Without a watch the panel still works, it just needs a manual refresh.
        (void)m_fsWatcher.addPath(loaded);
    }
    m_source = m_loadingSource;
    if (local) {
        m_results.reset();
    }
    m_path = loaded;
    updatePathBar();
    if (!sameDir) {
        if (m_historyTarget >= 0 && m_historyTarget < m_history.size()) {
            m_historyIndex = m_historyTarget;
        } else if (local) {
            recordHistory(m_path);
        }
        m_historyTarget = -1;
        emit pathChanged(m_path);
        m_renamedTo.clear();
        // A filter belongs to the folder it was typed in, as in DC.
        if (m_filterEdit->isVisible() || !m_filterEdit->text().isEmpty()) {
            const bool hadFocus = m_filterEdit->hasFocus();
            const QSignalBlocker block(m_filterEdit);
            m_filterEdit->clear();
            m_filterEdit->hide();
            m_model->clearNameFilterQuietly();
            if (hadFocus) {
                m_view->setFocus();
            }
        }
    }

    const bool showParent = !m_source->parentPath(m_path).isEmpty();
    m_keepCursor = sameDir;
    m_model->setItems(items, showParent, sameDir);
    m_keepCursor = true;
    startFolderSizes();
    emit navigationChanged();
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
    m_pathBar->setActive(active);
    m_model->setPanelActive(active);

    // The inactive panel shows its cursor row as a faint grey band instead of the full highlight.
    constexpr qreal InactiveCursorTint = 0.12;
    QPalette palette = QApplication::palette(m_view);
    if (!active) {
        const QColor base = palette.color(QPalette::Base);
        const QColor text = palette.color(QPalette::Text);
        const QColor tint = QColor::fromRgbF(
            base.redF() + (text.redF() - base.redF()) * InactiveCursorTint,
            base.greenF() + (text.greenF() - base.greenF()) * InactiveCursorTint,
            base.blueF() + (text.blueF() - base.blueF()) * InactiveCursorTint);
        for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive}) {
            palette.setColor(group, QPalette::Highlight, tint);
            palette.setColor(group, QPalette::HighlightedText, text);
        }
    }
    m_view->setPalette(palette);
}

void FilePanel::refreshColours()
{
    setActive(m_active);
}

QWidget *FilePanel::listView() const
{
    return m_view;
}

void FilePanel::focusView()
{
    m_view->setFocus();
}

void FilePanel::setShowHidden(bool show)
{
    m_model->setShowHidden(show);
}

void FilePanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Figures already shown stay until the recount replaces them.
    startFolderSizes();
}

void FilePanel::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    stopFolderSizes();
}

FilePanel::~FilePanel()
{
    stopFolderSizes();
}

void FilePanel::setShowFolderSizes(bool show)
{
    if (show == m_showFolderSizes) {
        return;
    }
    m_showFolderSizes = show;
    if (show) {
        startFolderSizes();
    } else {
        stopFolderSizes();
        m_model->clearDirSizes();
    }
}

void FilePanel::stopFolderSizes()
{
    if (m_sizeCancel) {
        m_sizeCancel->store(true);
        m_sizeCancel.reset();
    }
    ++m_sizeGeneration;
}

void FilePanel::startFolderSizes()
{
    stopFolderSizes();
    // Search results span many folders, and an unreachable share would only time out.
    if (!m_showFolderSizes || !isVisible() || isShowingResults() || m_unreachable || m_path.isEmpty()) {
        return;
    }
    QList<QPair<QString, QString>> folders;
    for (const QString &name : m_model->dirNames()) {
        folders.append({name, m_source->childPath(m_path, name)});
    }
    if (folders.isEmpty()) {
        return;
    }
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_sizeCancel = cancel;
    const quint64 generation = m_sizeGeneration;
    // Made here on the UI thread, and only read there, so it is safe if the panel is deleted meanwhile.
    const QPointer<FilePanel> guard(this);
    folderSizePool().start([folders, cancel, generation, guard] {
        for (const auto &folder : folders) {
            const qint64 size = folderSize(folder.second, *cancel);
            if (size < 0) {
                return;
            }
            const QString name = folder.first;
            QMetaObject::invokeMethod(qApp, [guard, generation, name, size] {
                if (guard != nullptr && guard->m_sizeGeneration == generation) {
                    guard->m_model->setDirSize(name, size);
                }
            }, Qt::QueuedConnection);
        }
    });
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
    if ((watched == m_view || watched == m_view->viewport()) && event->type() == QEvent::ContextMenu) {
        const auto *menuEvent = static_cast<QContextMenuEvent *>(event);
        QWidget *anchor = m_view->viewport();
        QPoint pos;
        int row = -1;
        if (menuEvent->reason() == QContextMenuEvent::Keyboard) {
            // The menu key opens the menu on the cursor row.
            row = m_view->currentIndex().row();
            const QRect cell = m_view->visualRect(m_model->index(std::max(row, 0), 0));
            pos = cell.isValid() ? cell.bottomLeft() : QPoint(0, 0);
        } else {
            pos = anchor->mapFromGlobal(menuEvent->globalPos());
            row = m_view->rowAt(pos.y());
        }
        const FileItem *item = m_model->itemAt(row);
        if (item != nullptr && !m_model->markedNames().contains(item->name)) {
            // As in Explorer, right-clicking an unmarked entry acts on that entry alone.
            m_model->markAll(false);
            m_view->setCurrentIndex(m_model->index(row, 0));
        }
        m_view->setFocus();
        emit contextMenuRequested(this, anchor, pos, item != nullptr);
        return true;
    }
    if (watched == m_view->viewport() && handleDropEvent(event)) {
        return true;
    }
    if (watched == m_view->viewport() && event->type() == QEvent::MouseMove) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (m_pressRow >= 0 && (mouse->buttons() & Qt::LeftButton)
            && (mouse->position().toPoint() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
            const int row = m_pressRow;
            m_pressRow = -1;
            startDrag(row);
            return true;
        }
        return QWidget::eventFilter(watched, event);
    }
    if (watched == m_view->viewport() && event->type() == QEvent::MouseButtonRelease) {
        m_pressRow = -1;
        return QWidget::eventFilter(watched, event);
    }
    if (watched == m_view->viewport() && event->type() == QEvent::MouseButtonPress) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        const int row = m_view->rowAt(mouse->position().toPoint().y());
        const Qt::KeyboardModifiers mods = mouse->modifiers();
        const bool plainLeft = mouse->button() == Qt::LeftButton && mods == Qt::NoModifier;
        m_pressRow = plainLeft && m_model->itemAt(row) != nullptr ? row : -1;
        m_pressPos = mouse->position().toPoint();
        if (mouse->button() == Qt::LeftButton && row >= 0
            && (mods == Qt::ControlModifier || mods == Qt::ShiftModifier)) {
            if (mods == Qt::ControlModifier) {
                // As in Explorer, the file under the cursor joins the first Ctrl+click selection.
                const int current = m_view->currentIndex().row();
                if (m_model->markedCount() == 0 && current != row && m_model->itemAt(current) != nullptr) {
                    m_model->toggleMark(current);
                }
                m_model->toggleMark(row);
            } else {
                const int anchor = m_view->currentIndex().isValid() ? m_view->currentIndex().row() : row;
                m_model->markRange(anchor, row, true);
            }
            m_view->setCurrentIndex(m_model->index(row, 0));
            m_view->setFocus();
            return true;
        }
        return QWidget::eventFilter(watched, event);
    }
    if (m_renameEdit != nullptr && watched == m_renameEdit) {
        if (event->type() == QEvent::KeyPress) {
            const auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                commitInlineRename();
                return true;
            }
            if (key->key() == Qt::Key_Escape) {
                cancelInlineRename();
                return true;
            }
        } else if (event->type() == QEvent::FocusOut) {
            // Clicking away cancels, so a half-typed name is never applied by accident.
            QMetaObject::invokeMethod(this, &FilePanel::cancelInlineRename, Qt::QueuedConnection);
        }
        return QWidget::eventFilter(watched, event);
    }
    if (watched == m_filterEdit && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if ((key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
            switch (key->key()) {
            case Qt::Key_Escape:
                closeQuickFilter(true);
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Down:
                // Back to the list with the filter kept.
                focusView();
                return true;
            default:
                break;
            }
        }
        return QWidget::eventFilter(watched, event);
    }
    if (watched != m_view || event->type() != QEvent::KeyPress) {
        return QWidget::eventFilter(watched, event);
    }

    const auto *key = static_cast<QKeyEvent *>(event);
    const Qt::KeyboardModifiers mods = key->modifiers() & ~Qt::KeypadModifier;
    if (mods == Qt::ShiftModifier) {
        const int current = m_view->currentIndex().isValid() ? m_view->currentIndex().row() : 0;
        const int last = m_model->rowCount() - 1;
        const int page = std::max(1, m_view->viewport()->height() / std::max(1, m_view->rowHeight(0)) - 1);
        int target = -1;
        switch (key->key()) {
        case Qt::Key_Up:
            target = current - 1;
            break;
        case Qt::Key_Down:
            target = current + 1;
            break;
        case Qt::Key_PageUp:
            target = current - page;
            break;
        case Qt::Key_PageDown:
            target = current + page;
            break;
        case Qt::Key_Home:
            target = 0;
            break;
        case Qt::Key_End:
            target = last;
            break;
        default:
            break;
        }
        if (target != -1 || key->key() == Qt::Key_Up) {
            if (last < 0) {
                return true;
            }
            target = std::clamp(target, 0, last);
            // Up/Down mark the row being left, like DC. Longer jumps mark the whole span.
            const bool single = key->key() == Qt::Key_Up || key->key() == Qt::Key_Down;
            m_model->markRange(current, single ? current : target, true);
            const QModelIndex index = m_model->index(target, 0);
            m_view->setCurrentIndex(index);
            m_view->scrollTo(index);
            return true;
        }
    }
    if (mods == Qt::ControlModifier && key->key() == Qt::Key_A) {
        markAll(true);
        return true;
    }
    if (mods == Qt::NoModifier && key->key() == Qt::Key_Escape) {
        // Esc first closes an open filter, then clears marks.
        if (m_filterEdit->isVisible()) {
            closeQuickFilter(true);
        } else {
            markAll(false);
        }
        return true;
    }
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

void FilePanel::markAll(bool mark)
{
    m_model->markAll(mark);
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
    const QString text = m_model->isFiltered() ? tr("%1 of %2 marked, %3 of %4 (filtered)")
                                               : tr("%1 of %2 marked, %3 of %4");
    m_status->setText(text.arg(m_model->markedCount())
                          .arg(m_model->fileCount())
                          .arg(locale.formattedDataSize(m_model->markedSize()),
                               locale.formattedDataSize(m_model->totalSize())));
}

void FilePanel::startDrag(int row)
{
    const FileItem *item = m_model->itemAt(row);
    if (item == nullptr) {
        return;
    }
    QStringList names = m_model->markedNames();
    if (!names.contains(item->name)) {
        names = {item->name};
    }
    QList<QUrl> urls;
    for (const QString &name : std::as_const(names)) {
        urls.append(QUrl::fromLocalFile(m_source->childPath(m_path, name)));
    }
    auto *mime = new QMimeData;
    mime->setUrls(urls);
    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    constexpr int DragIconSize = 32;
    const QIcon icon = m_model->data(m_model->index(row, 0), Qt::DecorationRole).value<QIcon>();
    drag->setPixmap(icon.pixmap(DragIconSize, devicePixelRatioF()));
    drag->setDragCursor(dragCursor(false, devicePixelRatioF()), Qt::MoveAction);
    drag->setDragCursor(dragCursor(true, devicePixelRatioF()), Qt::CopyAction);
    // The target does the copy or move and the folder watcher shows it, so the result is unused.
    (void)drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
}

QString FilePanel::dropTarget(const QPoint &pos, const QStringList &sources, int *row) const
{
    *row = -1;
    if (sources.isEmpty() || isShowingResults() || m_unreachable || m_path.isEmpty()) {
        return QString();
    }
    QString target = m_path;
    const int hit = m_view->rowAt(pos.y());
    if (m_model->isParentRow(hit)) {
        target = m_source->parentPath(m_path);
        *row = hit;
    } else if (const FileItem *item = m_model->itemAt(hit); item != nullptr && item->isDir) {
        target = m_source->childPath(m_path, item->name);
        *row = hit;
    }
    if (!transferTargetProblem(sources, target).isEmpty()) {
        *row = -1;
        return QString();
    }
    return target;
}

void FilePanel::releaseFolderUnder(const QStringList &paths)
{
    const QString here = QDir::cleanPath(m_path);
    for (const QString &path : paths) {
        const QString gone = QDir::cleanPath(QDir::fromNativeSeparators(path));
        if (here.compare(gone, Qt::CaseInsensitive) == 0
            || here.startsWith(gone + QLatin1Char('/'), Qt::CaseInsensitive)) {
            const QStringList watched = m_fsWatcher.directories();
            if (!watched.isEmpty()) {
                // Returns the paths it could not remove, which only matters for paths never added.
                (void)m_fsWatcher.removePaths(watched);
            }
            m_folderReleased = true;
            return;
        }
    }
}

void FilePanel::reloadIfReleased()
{
    if (m_folderReleased) {
        m_folderReleased = false;
        reload();
    }
}

QString FilePanel::pasteProblem(const QStringList &sources) const
{
    if (isShowingResults()) {
        return tr("Files cannot be pasted into search results.");
    }
    if (m_unreachable || m_path.isEmpty()) {
        return tr("This folder cannot be reached.");
    }
    return transferTargetProblem(sources, m_path);
}

bool FilePanel::handleDropEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::DragEnter:
    case QEvent::DragMove:
    case QEvent::Drop:
        break;
    case QEvent::DragLeave:
        m_model->setDropRow(-1);
        return true;
    default:
        return false;
    }
    auto *drop = static_cast<QDropEvent *>(event);
    QStringList sources;
    if (drop->mimeData()->hasUrls()) {
        for (const QUrl &url : drop->mimeData()->urls()) {
            if (!url.isLocalFile()) {
                sources.clear();
                break;
            }
            sources.append(QDir::fromNativeSeparators(url.toLocalFile()));
        }
    }
    if (sources.isEmpty()) {
        drop->ignore();
        return true;
    }
    if (event->type() == QEvent::DragEnter) {
        // Accepted here even over a bad spot, so move events keep coming and can accept later.
        drop->acceptProposedAction();
    }
    int row = -1;
    const QString target = dropTarget(drop->position().toPoint(), sources, &row);
    m_model->setDropRow(row);
    if (target.isEmpty()) {
        drop->ignore();
        if (event->type() == QEvent::Drop) {
            m_model->setDropRow(-1);
        }
        return true;
    }
    // Copy unless Ctrl is held, whichever program the files come from. Shift has no effect.
    const bool move = drop->modifiers().testFlag(Qt::ControlModifier);
    if (event->type() != QEvent::Drop) {
        drop->setDropAction(move ? Qt::MoveAction : Qt::CopyAction);
        drop->accept();
        return true;
    }
    m_model->setDropRow(-1);
    // Reported as a copy even for a move, so a source such as Explorer never deletes its originals.
    drop->setDropAction(Qt::CopyAction);
    drop->accept();
    // Queued so the dialog that follows does not open inside the drop, which would freeze the source.
    QMetaObject::invokeMethod(this, [this, sources, target, move] { emit filesDropped(this, sources, target, move); },
                              Qt::QueuedConnection);
    return true;
}

bool FilePanel::startInlineRename()
{
    if (isShowingResults()) {
        return false;
    }
    const int row = m_view->currentIndex().row();
    const FileItem *item = m_model->itemAt(row);
    if (item == nullptr || m_renameEdit != nullptr) {
        return true;
    }
    m_renameFrom = item->name;
    // The edit covers the Name and Ext cells, as the name is edited whole.
    const QRect nameCell = m_view->visualRect(m_model->index(row, static_cast<int>(SortColumn::Name)));
    const QRect extCell = m_view->visualRect(m_model->index(row, static_cast<int>(SortColumn::Extension)));
    m_renameEdit = new QLineEdit(item->name, m_view->viewport());
    m_renameEdit->setGeometry(nameCell.united(extCell));
    m_renameEdit->installEventFilter(this);
    const qsizetype dot = item->name.lastIndexOf(QLatin1Char('.'));
    if (!item->isDir && dot > 0) {
        // The extension stays out of the selection, as in Explorer and DC.
        m_renameEdit->setSelection(0, static_cast<int>(dot));
    } else {
        m_renameEdit->selectAll();
    }
    m_renameEdit->show();
    m_renameEdit->setFocus();
    return true;
}

void FilePanel::commitInlineRename()
{
    if (m_renameEdit == nullptr) {
        return;
    }
    const QString name = m_renameEdit->text().trimmed();
    if (name.isEmpty() || name == m_renameFrom) {
        cancelInlineRename();
        return;
    }
    static const QString forbidden = QStringLiteral("\\/:*?\"<>|");
    for (const QChar c : name) {
        if (forbidden.contains(c)) {
            showError(tr("A name cannot contain any of these characters: %1").arg(forbidden));
            m_renameEdit->setFocus();
            return;
        }
    }
    // A folder of that name would make the move go inside it. A case-only change is still a rename.
    const bool caseOnly = name.compare(m_renameFrom, Qt::CaseInsensitive) == 0;
    if (!caseOnly) {
        for (const QString &dir : m_model->dirNames()) {
            if (dir.compare(name, Qt::CaseInsensitive) == 0) {
                showError(tr("A folder called %1 already exists here.").arg(name));
                m_renameEdit->setFocus();
                return;
            }
        }
    }
    const QString path = m_source->childPath(m_path, m_renameFrom);
    m_renamedTo = name;
    cancelInlineRename();
    emit renameRequested(this, path, name);
}

void FilePanel::cancelInlineRename()
{
    if (m_renameEdit == nullptr) {
        return;
    }
    // Cleared first, because hiding the box moves focus and sends another FocusOut.
    QLineEdit *edit = m_renameEdit;
    m_renameEdit = nullptr;
    const bool hadFocus = edit->hasFocus();
    edit->hide();
    edit->deleteLater();
    if (hadFocus) {
        m_view->setFocus();
    }
}

void FilePanel::showQuickFilter()
{
    m_filterEdit->show();
    m_filterEdit->setFocus();
    m_filterEdit->selectAll();
}

void FilePanel::closeQuickFilter(bool focusList)
{
    const bool hadFocus = m_filterEdit->hasFocus();
    m_filterEdit->clear();
    m_filterEdit->hide();
    if (focusList || hadFocus) {
        focusView();
    }
}

void FilePanel::updatePathBar()
{
    const QString display = m_source->displayPath(m_path);
    QList<Crumb> crumbs;
    if (m_source != m_local) {
        crumbs.append(Crumb{display, QString()});
        m_pathBar->setLocation(display, crumbs);
        return;
    }
    // Bounded in case parentPath never reaches a root.
    constexpr int MaxDepth = 256;
    QString dir = m_path;
    for (int depth = 0; depth < MaxDepth && !dir.isEmpty(); ++depth) {
        // A server shows as "\\server", so the trail reads like the typed path.
        QString label = LocalFileSource::isUncServer(dir) ? m_local->displayPath(dir) : QDir(dir).dirName();
        if (label.isEmpty()) {
            // A root: "C:\" shows as "C:" and "/" stays "/".
            label = m_local->displayPath(dir);
            if (label.size() > 1 && (label.endsWith(QLatin1Char('\\')) || label.endsWith(QLatin1Char('/')))) {
                label.chop(1);
            }
        }
        crumbs.prepend(Crumb{label, dir});
        dir = m_local->parentPath(dir);
    }
    m_pathBar->setLocation(display, crumbs);
}

void FilePanel::recordHistory(const QString &path)
{
    if (m_historyIndex >= 0 && m_history.at(m_historyIndex) == path) {
        return;
    }
    // A new folder after going back drops the forward entries, as in a browser.
    m_history.resize(m_historyIndex + 1);
    m_history.append(path);
    if (m_history.size() > MaxHistory) {
        m_history.removeFirst();
    }
    m_historyIndex = static_cast<int>(m_history.size()) - 1;
}

bool FilePanel::canGoBack() const
{
    return m_historyIndex > 0;
}

bool FilePanel::canGoForward() const
{
    return m_historyIndex >= 0 && m_historyIndex < m_history.size() - 1;
}

void FilePanel::goBack()
{
    if (canGoBack()) {
        goToHistory(m_historyIndex - 1);
    }
}

void FilePanel::goForward()
{
    if (canGoForward()) {
        goToHistory(m_historyIndex + 1);
    }
}

void FilePanel::goToHistory(int index)
{
    setPath(m_history.at(index));
    // Set after setPath, which clears it for ordinary loads.
    m_historyTarget = index;
}

bool FilePanel::canGoUp() const
{
    return isShowingResults() || !m_source->parentPath(m_path).isEmpty();
}

bool FilePanel::canOpenTerminal() const
{
    return !m_networkPending && !m_unreachable;
}

void FilePanel::openTerminalHere()
{
    if (!canOpenTerminal()) {
        return;
    }
    QString error;
    if (!openTerminal(persistentPath(), &error)) {
        QMessageBox::warning(this, tr("Open Terminal"), error);
    }
}

void FilePanel::showError(const QString &message)
{
    m_status->setText(message);
}

} // namespace bifrons
