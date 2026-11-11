#include "ui/mainwindow.h"

#include <QCloseEvent>
#include <QDir>
#include <QInputDialog>
#include <QMessageBox>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>

#include "core/appinfo.h"
#include "core/localfilesource.h"
#include "core/transfer.h"
#include "ui/commandregistry.h"
#include "ui/filepanel.h"
#include "ui/operationrunner.h"

namespace dcpp {

namespace {

constexpr int MaxErrorsShown = 20;

QSettings settingsStore()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, appOrganisation(), appOrganisation());
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_source(std::make_shared<LocalFileSource>())
{
    m_left = new FilePanel(m_source, this);
    m_right = new FilePanel(m_source, this);
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->addWidget(m_left);
    m_splitter->addWidget(m_right);
    m_splitter->setChildrenCollapsible(false);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_splitter, 1);
    setCentralWidget(central);

    for (FilePanel *panel : {m_left, m_right}) {
        connect(panel, &FilePanel::activated, this, &MainWindow::setActivePanel);
        connect(panel, &FilePanel::switchPanelRequested, this, [this] {
            otherPanel()->focusView();
        });
    }

    createCommands();

    setWindowTitle(appDisplayName());
    statusBar()->hide();
    resize(1100, 700);
    loadSettings();
}

void MainWindow::createCommands()
{
    m_commands = new CommandRegistry(this);
    m_commands->add(QStringLiteral("cm_Copy"), tr("Copy"), Qt::Key_F5, [this] { cmCopy(); });
    m_commands->add(QStringLiteral("cm_Rename"), tr("Move"), Qt::Key_F6, [this] { cmRename(); });
    m_commands->add(QStringLiteral("cm_RenameOnly"), tr("Rename"), Qt::SHIFT | Qt::Key_F6,
                    [this] { cmRenameOnly(); });
    m_commands->add(QStringLiteral("cm_MakeDir"), tr("New Folder"), Qt::Key_F7, [this] { cmMakeDir(); });
    m_commands->add(QStringLiteral("cm_Delete"), tr("Delete"), Qt::Key_F8, [this] { cmDelete(); });
    m_commands->add(QStringLiteral("cm_Refresh"), tr("Refresh"), Qt::CTRL | Qt::Key_R, [this] { cmRefresh(); });
    m_commands->add(QStringLiteral("cm_Exit"), tr("Exit"), Qt::ALT | Qt::Key_X, [this] { close(); });
}

void MainWindow::setActivePanel(FilePanel *panel)
{
    m_active = panel;
    m_left->setActive(panel == m_left);
    m_right->setActive(panel == m_right);
}

FilePanel *MainWindow::otherPanel() const
{
    return m_active == m_left ? m_right : m_left;
}

void MainWindow::cmCopy()
{
    const QStringList sources = m_active->selectedPaths();
    transfer(LocalFileOperation::Kind::Copy, sources, tr("Copy"),
             tr("Copy %1 item(s) to:").arg(sources.size()), m_source->displayPath(otherPanel()->path()));
}

void MainWindow::cmRename()
{
    const QStringList sources = m_active->selectedPaths();
    transfer(LocalFileOperation::Kind::Move, sources, tr("Move"),
             tr("Move %1 item(s) to:").arg(sources.size()), m_source->displayPath(otherPanel()->path()));
}

void MainWindow::cmRenameOnly()
{
    // Renames only the item under the cursor, whatever is marked.
    const QString name = m_active->currentName();
    if (name.isEmpty()) {
        return;
    }
    transfer(LocalFileOperation::Kind::Move, {m_source->childPath(m_active->path(), name)}, tr("Rename"),
             tr("New name:"), name);
}

void MainWindow::transfer(LocalFileOperation::Kind kind, const QStringList &sources, const QString &title,
                          const QString &label, const QString &defaultDestination)
{
    if (sources.isEmpty()) {
        return;
    }
    bool ok = false;
    const QString destination =
        QInputDialog::getText(this, title, label, QLineEdit::Normal, defaultDestination, &ok);
    if (!ok) {
        return;
    }
    const TransferPlan plan = planTransfer(sources, destination, m_active->path());
    if (!plan.ok) {
        QMessageBox::warning(this, title, plan.error);
        return;
    }
    QString error;
    if (!plan.createDir.isEmpty() && !m_source->makePath(plan.createDir, &error)) {
        QMessageBox::warning(this, title, error);
        return;
    }
    runOperation(new LocalFileOperation(kind, plan.items), title);
}

void MainWindow::cmMakeDir()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    const QString path = LocalFileSource::normalise(QDir(m_active->path()).filePath(name));
    QString error;
    if (!m_source->makePath(path, &error)) {
        QMessageBox::warning(this, tr("New Folder"), error);
        return;
    }
    // Select the top-level folder that was created, for "a/b/c" that is "a".
    const QString first = QDir::fromNativeSeparators(name).section(QLatin1Char('/'), 0, 0, QString::SectionSkipEmpty);
    m_active->setPath(m_active->path(), first);
}

void MainWindow::cmDelete()
{
    const QStringList paths = m_active->selectedPaths();
    if (paths.isEmpty()) {
        return;
    }
    const QString question = paths.size() == 1
        ? tr("Move \"%1\" to the recycle bin?").arg(QFileInfo(paths.first()).fileName())
        : tr("Move %1 items to the recycle bin?").arg(paths.size());
    if (QMessageBox::question(this, tr("Delete"), question) != QMessageBox::Yes) {
        return;
    }
    QList<TransferItem> items;
    for (const QString &path : paths) {
        items.append({path, QString()});
    }
    runOperation(new LocalFileOperation(LocalFileOperation::Kind::Delete, items), tr("Delete"));
}

void MainWindow::cmRefresh()
{
    m_left->reload();
    m_right->reload();
}

void MainWindow::runOperation(FileOperation *op, const QString &title)
{
    if (m_runner != nullptr) {
        delete op;
        return;
    }
    m_runner = new OperationRunner(op, title, this);
    connect(m_runner, &OperationRunner::finished, this, &MainWindow::onOperationFinished);
    m_runner->start();
}

void MainWindow::onOperationFinished(const FileOperation::Result &result)
{
    m_runner->deleteLater();
    m_runner = nullptr;
    cmRefresh();

    if (m_closePending) {
        close();
        return;
    }
    if (!result.errors.isEmpty()) {
        QStringList shown = result.errors.mid(0, MaxErrorsShown);
        if (result.errors.size() > MaxErrorsShown) {
            shown.append(tr("…and %1 more.").arg(result.errors.size() - MaxErrorsShown));
        }
        QMessageBox::warning(this, tr("Some items were not processed"), shown.join(QLatin1Char('\n')));
    }
    m_active->focusView();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Never block on the worker: cancel it and close once it reports back.
    if (m_runner != nullptr && m_runner->isRunning()) {
        m_closePending = true;
        m_runner->cancel();
        event->ignore();
        return;
    }
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::loadSettings()
{
    QSettings s = settingsStore();
    const QByteArray geometry = s.value(QStringLiteral("window/geometry")).toByteArray();
    if (!geometry.isEmpty()) {
        // An invalid saved geometry just leaves the default size.
        (void)restoreGeometry(geometry);
    }
    const QByteArray split = s.value(QStringLiteral("window/splitter")).toByteArray();
    if (!split.isEmpty()) {
        (void)m_splitter->restoreState(split);
    }

    const QString home = QDir::homePath();
    const struct
    {
        FilePanel *panel;
        QString key;
    } panels[] = {{m_left, QStringLiteral("left")}, {m_right, QStringLiteral("right")}};
    for (const auto &p : panels) {
        s.beginGroup(p.key);
        const int column = s.value(QStringLiteral("sortColumn"), 0).toInt();
        const int order = s.value(QStringLiteral("sortOrder"), 0).toInt();
        const bool columnValid = column >= 0 && column < static_cast<int>(SortColumn::Count);
        p.panel->setSort(columnValid ? static_cast<SortColumn>(column) : SortColumn::Name,
                         order == 1 ? Qt::DescendingOrder : Qt::AscendingOrder);
        p.panel->setPath(LocalFileSource::nearestExistingDir(s.value(QStringLiteral("path"), home).toString()));
        s.endGroup();
    }

    FilePanel *active = s.value(QStringLiteral("activePanel")).toString() == QLatin1String("right") ? m_right : m_left;
    setActivePanel(active);
    active->focusView();
}

void MainWindow::saveSettings() const
{
    QSettings s = settingsStore();
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/splitter"), m_splitter->saveState());
    s.setValue(QStringLiteral("activePanel"), m_active == m_right ? QStringLiteral("right") : QStringLiteral("left"));

    const struct
    {
        const FilePanel *panel;
        QString key;
    } panels[] = {{m_left, QStringLiteral("left")}, {m_right, QStringLiteral("right")}};
    for (const auto &p : panels) {
        s.beginGroup(p.key);
        s.setValue(QStringLiteral("path"), p.panel->path());
        s.setValue(QStringLiteral("sortColumn"), static_cast<int>(p.panel->sortColumn()));
        s.setValue(QStringLiteral("sortOrder"), p.panel->sortOrder() == Qt::DescendingOrder ? 1 : 0);
        s.endGroup();
    }
    s.sync();
    if (s.status() != QSettings::NoError) {
        qWarning("Could not save settings to %s", qPrintable(s.fileName()));
    }
}

} // namespace dcpp
