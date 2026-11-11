#include "ui/mainwindow.h"

#include <QClipboard>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QDir>
#include <QFrame>
#include <QInputDialog>
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QVBoxLayout>

#include "core/appinfo.h"
#include "core/localfilesource.h"
#include "core/searchresultfilesource.h"
#include "core/transfer.h"
#include "ui/commandregistry.h"
#if defined(BIFRONS_DEMO_OPERATION)
#include "ui/demooperation.h"
#endif
#include "ui/filepanel.h"
#include "ui/favouritesdialog.h"
#include "ui/finddialog.h"
#include "ui/multirenamedialog.h"
#include "ui/operationqueue.h"
#include "ui/paneltabs.h"
#include "ui/paneltoolbar.h"
#include "ui/shellmenu.h"
#include "ui/viewerwidget.h"
#include "ui/viewerwindow.h"

#include <algorithm>

namespace bifrons {

namespace {

// Double-clicking the divider splits the space evenly again.
class CentringSplitterHandle : public QSplitterHandle
{
public:
    using QSplitterHandle::QSplitterHandle;

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        QSplitter *owner = splitter();
        const int count = owner->count();
        if (event->button() != Qt::LeftButton || count == 0) {
            QSplitterHandle::mouseDoubleClickEvent(event);
            return;
        }
        int total = 0;
        for (const int size : owner->sizes()) {
            total += size;
        }
        owner->setSizes(QList<int>(count, total / count));
        event->accept();
    }
};

class CentringSplitter : public QSplitter
{
public:
    using QSplitter::QSplitter;

protected:
    QSplitterHandle *createHandle() override
    {
        return new CentringSplitterHandle(orientation(), this);
    }
};

constexpr int MaxErrorsShown = 20;
constexpr int MaxTabsRestored = 100;
constexpr int QuickViewDelayMs = 120;
constexpr qint64 QuickViewMaxBytes = 2 * 1024 * 1024;

QSettings settingsStore()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, appOrganisation(), appOrganisation());
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_source(std::make_shared<LocalFileSource>())
{
    m_left = new PanelTabs(m_source, this);
    m_right = new PanelTabs(m_source, this);
    m_leftStack = new QStackedWidget(this);
    m_leftStack->addWidget(wrapWithToolbar(m_left));
    m_rightStack = new QStackedWidget(this);
    m_rightStack->addWidget(wrapWithToolbar(m_right));
    m_splitter = new CentringSplitter(Qt::Horizontal, this);
    m_splitter->addWidget(m_leftStack);
    m_splitter->addWidget(m_rightStack);
    m_splitter->setChildrenCollapsible(false);

    m_quickTimer.setSingleShot(true);
    m_quickTimer.setInterval(QuickViewDelayMs);
    connect(&m_quickTimer, &QTimer::timeout, this, &MainWindow::updateQuickView);

    auto *central = new QWidget(this);
    auto *outer = new QVBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    // Full-width rule separating the menu bar from the panels.
    auto *rule = new QFrame(central);
    rule->setFrameShape(QFrame::NoFrame);
    rule->setFixedHeight(1);
    rule->setStyleSheet(QStringLiteral("QFrame { background: palette(dark); }"));
    outer->addWidget(rule);
    auto *layout = new QVBoxLayout;
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_splitter, 1);
    outer->addLayout(layout, 1);
    setCentralWidget(central);

    for (PanelTabs *side : {m_left, m_right}) {
        connect(side, &PanelTabs::panelActivated, this, &MainWindow::setActivePanel);
        connect(side, &PanelTabs::contextMenuRequested, this, &MainWindow::showContextMenu);
        connect(side, &PanelTabs::switchPanelRequested, this, [this] {
            if (m_quickSide != nullptr) {
                m_quickView->setFocus();
            } else {
                otherPanel()->focusView();
            }
        });
        connect(side, &PanelTabs::currentItemChanged, this, [this](FilePanel *panel) {
            if (m_quickSide != nullptr && panel == m_active) {
                m_quickTimer.start();
            }
        });
    }

    createCommands();
    createMenus();

    setWindowTitle(appDisplayName());
    statusBar()->hide();
    resize(1100, 700);
    loadSettings();
}

void MainWindow::createCommands()
{
    m_commands = new CommandRegistry(this);
    m_commands->add(QStringLiteral("cm_Copy"), tr("&Copy…"), Qt::Key_F5, [this] { cmCopy(); });
    m_commands->add(QStringLiteral("cm_Rename"), tr("&Move…"), Qt::Key_F6, [this] { cmRename(); });
    m_commands->add(QStringLiteral("cm_RenameOnly"), tr("&Rename…"), Qt::SHIFT | Qt::Key_F6,
                    [this] { cmRenameOnly(); });
    m_commands->add(QStringLiteral("cm_MakeDir"), tr("&New Folder…"), Qt::Key_F7, [this] { cmMakeDir(); });
    m_commands->add(QStringLiteral("cm_Delete"), tr("&Delete…"), Qt::Key_F8, [this] { cmDelete(); });
    m_commands->add(QStringLiteral("cm_Refresh"), tr("&Refresh"), Qt::CTRL | Qt::Key_R, [this] { cmRefresh(); });
#if defined(BIFRONS_DEMO_OPERATION)
    m_commands->add(QStringLiteral("cm_TestBackgroundOperation"), tr("Test &Background Operation"), QKeySequence(),
                    [this] { runOperation(new DemoOperation, tr("Test operation")); });
#endif
    m_commands->add(QStringLiteral("cm_RunTerm"), tr("Open &Terminal Here"), QKeySequence(),
                    [this] { m_active->openTerminalHere(); });
    m_commands->add(QStringLiteral("cm_ViewHistoryPrev"), tr("&Back"), Qt::ALT | Qt::Key_Left,
                    [this] { m_active->goBack(); });
    m_commands->add(QStringLiteral("cm_ViewHistoryNext"), tr("&Forward"), Qt::ALT | Qt::Key_Right,
                    [this] { m_active->goForward(); });
    m_commands->add(QStringLiteral("cm_Exit"), tr("E&xit"), Qt::ALT | Qt::Key_X, [this] { close(); });
    m_commands->add(QStringLiteral("cm_ShowFolderSizes"), tr("Show &Folder Sizes"), QKeySequence(),
                    [this] { setShowFolderSizes(!m_showFolderSizes); })
        ->setCheckable(true);
    QAction *hidden = m_commands->add(QStringLiteral("cm_ShowSysFiles"), tr("Show &Hidden Files"),
                                      Qt::CTRL | Qt::Key_Period, [this] { setShowHidden(!m_showHidden); });
    hidden->setCheckable(true);
    // + and - stay panel keys (a window shortcut would steal them from text fields), so no shortcut here.
    m_commands->add(QStringLiteral("cm_MarkPlus"), tr("&Select a Group…\t+"), QKeySequence(),
                    [this] { m_active->markByMaskPrompt(true); });
    m_commands->add(QStringLiteral("cm_MarkMinus"), tr("&Unselect a Group…\t-"), QKeySequence(),
                    [this] { m_active->markByMaskPrompt(false); });
    m_commands->add(QStringLiteral("cm_MarkMarkAll"), tr("Select &All\tCtrl+A"), QKeySequence(),
                    [this] { m_active->markAll(true); });
    m_commands->add(QStringLiteral("cm_MarkUnmarkAll"), tr("U&nselect All"), QKeySequence(),
                    [this] { m_active->markAll(false); });
    m_commands->add(QStringLiteral("cm_View"), tr("&View File"), Qt::Key_F3, [this] { cmView(); });
    m_commands->add(QStringLiteral("cm_QuickView"), tr("&Quick View"), Qt::CTRL | Qt::Key_Q,
                    [this] { cmQuickView(); });
    m_commands->action(QStringLiteral("cm_QuickView"))->setCheckable(true);
    m_commands->add(QStringLiteral("cm_DirHotList"), tr("&Favourites…"), Qt::CTRL | Qt::Key_D, [this] {
        QMenu menu(this);
        fillFavouritesMenu(&menu);
        menu.exec(m_active->mapToGlobal(QPoint(0, 0)));
    });
    m_commands->add(QStringLiteral("cm_About"), tr("&About %1").arg(appDisplayName()), QKeySequence(), [this] {
        QMessageBox::about(this, tr("About %1").arg(appDisplayName()),
                           tr("%1 %2\n\nA dual-pane file manager inspired by Double Commander.\nMIT licence.")
                               .arg(appDisplayName(), appVersion()));
    });
    m_commands->add(QStringLiteral("cm_MultiRename"), tr("Multi-Rena&me…"), Qt::CTRL | Qt::Key_M,
                    [this] { cmMultiRename(); });
    m_commands->add(QStringLiteral("cm_Search"), tr("&Find Files…"), Qt::ALT | Qt::Key_F7, [this] { cmSearch(); });
    m_commands->add(QStringLiteral("cm_NewTab"), tr("&New Tab"), Qt::CTRL | Qt::Key_T,
                    [this] { activeSide()->newTab(); });
    m_commands->add(QStringLiteral("cm_CloseTab"), tr("&Close Tab"), Qt::CTRL | Qt::Key_W,
                    [this] { activeSide()->closeCurrentTab(); });
    m_commands->add(QStringLiteral("cm_NextTab"), tr("Ne&xt Tab"), Qt::CTRL | Qt::Key_Tab,
                    [this] { activeSide()->nextTab(); });
    m_commands->add(QStringLiteral("cm_PrevTab"), tr("&Previous Tab"), Qt::CTRL | Qt::SHIFT | Qt::Key_Tab,
                    [this] { activeSide()->previousTab(); });
}

void MainWindow::createMenus()
{
    const auto addMenu = [this](const QString &title, std::initializer_list<const char *> commands) {
        QMenu *menu = menuBar()->addMenu(title);
        for (const char *name : commands) {
            if (name == nullptr) {
                menu->addSeparator();
                continue;
            }
            QAction *action = m_commands->action(QLatin1String(name));
            Q_ASSERT(action != nullptr);
            menu->addAction(action);
        }
        return menu;
    };
    addMenu(tr("&File"), {"cm_View", nullptr, "cm_Copy", "cm_Rename", "cm_RenameOnly", "cm_MakeDir", "cm_Delete", nullptr,
                          "cm_MultiRename", nullptr, "cm_Exit"});
    addMenu(tr("&Mark"), {"cm_MarkPlus", "cm_MarkMinus", nullptr, "cm_MarkMarkAll", "cm_MarkUnmarkAll"});
    QMenu *commands = addMenu(tr("&Commands"), {"cm_Search", "cm_RunTerm", nullptr, "cm_ViewHistoryPrev",
                                                "cm_ViewHistoryNext", nullptr, "cm_Refresh"});
#if defined(BIFRONS_DEMO_OPERATION)
    commands->addSeparator();
    commands->addAction(m_commands->action(QStringLiteral("cm_TestBackgroundOperation")));
#else
    Q_UNUSED(commands);
#endif
    addMenu(tr("&Tabs"), {"cm_NewTab", "cm_CloseTab", nullptr, "cm_NextTab", "cm_PrevTab"});
    QMenu *favourites = menuBar()->addMenu(tr("F&avourites"));
    connect(favourites, &QMenu::aboutToShow, this, [this, favourites] { fillFavouritesMenu(favourites); });
    addMenu(tr("&View"), {"cm_QuickView", nullptr, "cm_ShowSysFiles", "cm_ShowFolderSizes"});
    addMenu(tr("&Help"), {"cm_About"});
}

void MainWindow::fillFavouritesMenu(QMenu *menu)
{
    menu->clear();
    for (const Favourite &fav : std::as_const(m_favourites)) {
        QString label = fav.name;
        label.replace(QLatin1Char('&'), QStringLiteral("&&"));
        QAction *action = menu->addAction(QApplication::style()->standardIcon(QStyle::SP_DirIcon), label);
        action->setToolTip(QDir::toNativeSeparators(fav.path));
        const QString path = fav.path;
        connect(action, &QAction::triggered, this, [this, path] {
            m_active->setPath(path);
            m_active->focusView();
        });
    }
    if (m_favourites.isEmpty()) {
        menu->addAction(tr("(No favourites)"))->setEnabled(false);
    }
    menu->setToolTipsVisible(true);
    menu->addSeparator();

    const QString current = m_active->persistentPath();
    const bool listed = indexOfFavourite(m_favourites, current) >= 0;
    QAction *add = menu->addAction(listed ? tr("Current folder is a favourite") : tr("&Add current folder"));
    add->setEnabled(!listed);
    connect(add, &QAction::triggered, this, [this, current] {
        if (addFavourite(m_favourites, current)) {
            saveFavouritesNow();
        }
    });
    connect(menu->addAction(tr("&Organise favourites…")), &QAction::triggered, this, [this] {
        FavouritesDialog dialog(m_favourites, this);
        if (dialog.exec() == QDialog::Accepted) {
            m_favourites = dialog.favourites();
            saveFavouritesNow();
        }
    });
}

void MainWindow::saveFavouritesNow() const
{
    QSettings s = settingsStore();
    saveFavourites(s, m_favourites);
    s.sync();
    if (s.status() != QSettings::NoError) {
        qWarning("Could not save favourites to %s", qPrintable(s.fileName()));
    }
}

void MainWindow::setShowFolderSizes(bool show)
{
    m_showFolderSizes = show;
    m_commands->action(QStringLiteral("cm_ShowFolderSizes"))->setChecked(show);
    m_left->setShowFolderSizes(show);
    m_right->setShowFolderSizes(show);
}

void MainWindow::setShowHidden(bool show)
{
    m_showHidden = show;
    m_commands->action(QStringLiteral("cm_ShowSysFiles"))->setChecked(show);
    m_left->setShowHidden(show);
    m_right->setShowHidden(show);
}

PanelTabs *MainWindow::activeSide() const
{
    return m_activeSide == m_right ? m_right : m_left;
}

void MainWindow::setActivePanel(FilePanel *panel)
{
    m_active = panel;
    m_activeSide = m_right->indexOf(panel) >= 0 ? m_right : m_left;
    for (PanelTabs *side : {m_left, m_right}) {
        side->setFocusedSide(side == m_activeSide);
        for (FilePanel *p : side->panels()) {
            p->setActive(p == panel);
        }
    }
    if (m_quickSide != nullptr) {
        m_quickTimer.start();
    }
}

QWidget *MainWindow::wrapWithToolbar(PanelTabs *side)
{
    auto *container = new QWidget(this);
    auto *toolbar = new PanelToolbar(container);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(toolbar);
    layout->addWidget(side, 1);
    auto *queue = new OperationQueue(this, container);
    layout->addWidget(queue);
    m_queues.insert(side, queue);
    connect(queue, &OperationQueue::finished, this, &MainWindow::onOperationFinished);
    connect(side, &PanelTabs::currentPanelChanged, toolbar, &PanelToolbar::setPanel);
    toolbar->setPanel(side->currentPanel());
    return container;
}

FilePanel *MainWindow::otherPanel() const
{
    return (activeSide() == m_left ? m_right : m_left)->currentPanel();
}

void MainWindow::cmCopy()
{
    const QStringList sources = m_active->selectedPaths();
    transfer(LocalFileOperation::Kind::Copy, sources, m_active->persistentPath(), tr("Copy"),
             tr("Copy %1 item(s) to:").arg(sources.size()),
             m_source->displayPath(otherPanel()->persistentPath()));
}

void MainWindow::cmRename()
{
    const QStringList sources = m_active->selectedPaths();
    transfer(LocalFileOperation::Kind::Move, sources, m_active->persistentPath(), tr("Move"),
             tr("Move %1 item(s) to:").arg(sources.size()),
             m_source->displayPath(otherPanel()->persistentPath()));
}

void MainWindow::cmRenameOnly()
{
    // Renames only the item under the cursor, whatever is marked.
    const QString path = m_active->currentPath();
    if (path.isEmpty()) {
        return;
    }
    const QFileInfo info(path);
    transfer(LocalFileOperation::Kind::Move, {path}, info.absolutePath(), tr("Rename"), tr("New name:"),
             info.fileName());
}

void MainWindow::transfer(LocalFileOperation::Kind kind, const QStringList &sources, const QString &baseDir,
                          const QString &title, const QString &label, const QString &defaultDestination)
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
    const TransferPlan plan = planTransfer(sources, destination, baseDir);
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
    if (m_active->isShowingResults()) {
        QMessageBox::information(this, tr("New Folder"), tr("Folders cannot be created in search results."));
        return;
    }
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

void MainWindow::cmView()
{
    const QString path = m_active->currentPath();
    if (path.isEmpty() || QFileInfo(path).isDir()) {
        return;
    }
    const QStringList files = m_active->filePaths();
    const int index = static_cast<int>(files.indexOf(path));
    auto *viewer = new ViewerWindow(index >= 0 ? files : QStringList{path}, std::max(index, 0), this);
    viewer->show();
}

void MainWindow::cmQuickView()
{
    QAction *action = m_commands->action(QStringLiteral("cm_QuickView"));
    if (m_quickSide != nullptr) {
        m_quickSide->removeWidget(m_quickView);
        m_quickView->hide();
        m_quickSide->setCurrentIndex(0);
        m_quickSide = nullptr;
        m_quickTimer.stop();
        action->setChecked(false);
        m_active->focusView();
        return;
    }
    if (m_quickView == nullptr) {
        m_quickView = new ViewerWidget(QuickViewMaxBytes, this);
        connect(m_quickView, &ViewerWidget::markdownLinkActivated, m_quickView, &ViewerWidget::showFile);
        connect(m_quickView, &ViewerWidget::tabPressed, this, [this] { m_active->focusView(); });
    }
    m_quickSide = activeSide() == m_left ? m_rightStack : m_leftStack;
    m_quickSide->addWidget(m_quickView);
    m_quickSide->setCurrentWidget(m_quickView);
    action->setChecked(true);
    updateQuickView();
    m_active->focusView();
}

void MainWindow::updateQuickView()
{
    if (m_quickSide != nullptr) {
        m_quickView->showFile(m_active->currentPath());
    }
}

void MainWindow::cmMultiRename()
{
    QList<RenameItem> items;
    for (const QString &path : m_active->selectedPaths()) {
        const QFileInfo info(path);
        items.append({path, info.isDir(), info.lastModified()});
    }
    if (items.isEmpty()) {
        return;
    }
    MultiRenameDialog dialog(items, !m_lastRename.isEmpty(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (dialog.undoRequested()) {
        const RenameResult undone = undoRenames(m_lastRename);
        if (undone.ok) {
            m_lastRename.clear();
        } else {
            QMessageBox::warning(this, tr("Multi-Rename"), tr("Nothing was undone.\n\n%1").arg(undone.error));
        }
    } else {
        m_lastRename = dialog.performed();
    }
    cmRefresh();
}

void MainWindow::cmSearch()
{
    if (m_find == nullptr) {
        m_find = new FindDialog(this);
        connect(m_find, &FindDialog::goToFile, this, [this](const QString &path) {
            const QFileInfo info(path);
            m_active->setPath(info.absolutePath(), info.fileName());
            m_active->focusView();
        });
        connect(m_find, &FindDialog::feedToPanel, this, [this](const QString &baseDir, const QStringList &paths) {
            m_active->showSearchResults(std::make_shared<SearchResultFileSource>(baseDir, paths));
            m_active->focusView();
        });
        connect(m_find, &FindDialog::searchFinished, this, [this] {
            if (m_closePending) {
                close();
            }
        });
    }
    m_find->setStartDir(m_active->persistentPath());
    m_find->show();
    m_find->raise();
    m_find->activateWindow();
}

void MainWindow::cmRefresh()
{
    m_left->currentPanel()->reload();
    m_right->currentPanel()->reload();
}

void MainWindow::runOperation(FileOperation *op, const QString &title)
{
    OperationQueue *queue = m_queues.value(activeSide(), nullptr);
    if (op == nullptr || queue == nullptr || m_closePending) {
        delete op;
        return;
    }
    queue->enqueue(op, title);
}

void MainWindow::showContextMenu(FilePanel *panel, QWidget *anchor, const QPoint &localPos, bool onItem)
{
    if (panel == nullptr || anchor == nullptr) {
        return;
    }
    setActivePanel(panel);
    const auto command = [this](const char *name) {
        return [this, name] {
            if (QAction *action = m_commands->action(QLatin1String(name))) {
                action->trigger();
            }
        };
    };
    const auto copyPaths = [](const QStringList &paths) {
        return [paths] {
            QStringList native;
            for (const QString &path : paths) {
                native.append(QDir::toNativeSeparators(path));
            }
            QGuiApplication::clipboard()->setText(native.join(QLatin1Char('\n')));
        };
    };

    if (!onItem) {
        const QString folder = panel->persistentPath();
        showEntryMenu(anchor, localPos, {
            {tr("New &Folder\tF7"), command("cm_MakeDir")},
            {tr("Open &Terminal Here"), command("cm_RunTerm")},
            {tr("&Refresh\tCtrl+R"), command("cm_Refresh")},
            {QString(), {}},
            {tr("Copy Folder &Path"), copyPaths({folder})},
        });
        return;
    }

    const QStringList paths = panel->selectedPaths();
    const QList<MenuEntry> entries = {
        {tr("&View\tF3"), command("cm_View")},
        {tr("&Copy to Other Pane…\tF5"), command("cm_Copy")},
        {tr("&Move to Other Pane…\tF6"), command("cm_Rename")},
        {tr("Re&name…\tShift+F6"), command("cm_RenameOnly")},
        {tr("&Delete\tF8"), command("cm_Delete")},
        {QString(), {}},
        {tr("Copy &Path"), copyPaths(paths)},
    };
    // The shell resolves paths on this thread, so skip it where that could wait on the network.
    const bool shellSafe = panel->canOpenTerminal() && !panel->isShowingResults();
    if (!shellSafe || !showShellContextMenu(anchor, localPos, paths, entries, command("cm_Delete"))) {
        showEntryMenu(anchor, localPos, entries);
    }
}

bool MainWindow::operationsBusy() const
{
    for (const OperationQueue *queue : m_queues) {
        if (queue->isBusy()) {
            return true;
        }
    }
    return false;
}

void MainWindow::onOperationFinished(const FileOperation::Result &result)
{
    cmRefresh();

    if (m_closePending) {
        // Close once the last side has stopped.
        if (!operationsBusy()) {
            close();
        }
        return;
    }
    if (!result.errors.isEmpty()) {
        QStringList shown = result.errors.mid(0, MaxErrorsShown);
        if (result.errors.size() > MaxErrorsShown) {
            shown.append(tr("…and %1 more.").arg(result.errors.size() - MaxErrorsShown));
        }
        QMessageBox::warning(this, tr("Some items were not processed"), shown.join(QLatin1Char('\n')));
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Never block on a worker: drop queued work, cancel what runs and close once every side reports back.
    if (operationsBusy()) {
        m_closePending = true;
        for (OperationQueue *queue : std::as_const(m_queues)) {
            queue->cancelAll();
        }
        event->ignore();
        return;
    }
    if (m_find != nullptr && m_find->isSearching()) {
        m_closePending = true;
        m_find->stopSearch();
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

    setShowHidden(s.value(QStringLiteral("view/showHidden"), false).toBool());
    setShowFolderSizes(s.value(QStringLiteral("view/folderSizes"), false).toBool());
    m_favourites = loadFavourites(s);

    for (PanelTabs *side : {m_left, m_right}) {
        s.beginGroup(side == m_left ? QStringLiteral("left") : QStringLiteral("right"));
        QList<TabState> tabs;
        const int n = s.beginReadArray(QStringLiteral("tabs"));
        for (int i = 0; i < std::min(n, MaxTabsRestored); ++i) {
            s.setArrayIndex(i);
            const int column = s.value(QStringLiteral("sortColumn"), 0).toInt();
            const bool columnValid = column >= 0 && column < static_cast<int>(SortColumn::Count);
            TabState tab;
            tab.path = s.value(QStringLiteral("path")).toString();
            tab.sortColumn = columnValid ? static_cast<SortColumn>(column) : SortColumn::Name;
            tab.sortOrder = s.value(QStringLiteral("sortOrder"), 0).toInt() == 1 ? Qt::DescendingOrder
                                                                                  : Qt::AscendingOrder;
            tabs.append(tab);
        }
        s.endArray();
        side->restore(tabs, s.value(QStringLiteral("currentTab"), 0).toInt());
        s.endGroup();
    }

    PanelTabs *side = s.value(QStringLiteral("activePanel")).toString() == QLatin1String("right") ? m_right : m_left;
    setActivePanel(side->currentPanel());
    side->currentPanel()->focusView();
}

void MainWindow::saveSettings() const
{
    QSettings s = settingsStore();
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/splitter"), m_splitter->saveState());
    s.setValue(QStringLiteral("view/showHidden"), m_showHidden);
    s.setValue(QStringLiteral("view/folderSizes"), m_showFolderSizes);
    s.setValue(QStringLiteral("activePanel"),
               activeSide() == m_right ? QStringLiteral("right") : QStringLiteral("left"));

    for (const PanelTabs *side : {m_left, m_right}) {
        s.beginGroup(side == m_left ? QStringLiteral("left") : QStringLiteral("right"));
        s.remove(QString());
        const QList<TabState> tabs = side->state();
        s.beginWriteArray(QStringLiteral("tabs"), static_cast<int>(tabs.size()));
        for (int i = 0; i < tabs.size(); ++i) {
            s.setArrayIndex(i);
            s.setValue(QStringLiteral("path"), tabs.at(i).path);
            s.setValue(QStringLiteral("sortColumn"), static_cast<int>(tabs.at(i).sortColumn));
            s.setValue(QStringLiteral("sortOrder"), tabs.at(i).sortOrder == Qt::DescendingOrder ? 1 : 0);
        }
        s.endArray();
        s.setValue(QStringLiteral("currentTab"), side->currentIndex());
        s.endGroup();
    }
    s.sync();
    if (s.status() != QSettings::NoError) {
        qWarning("Could not save settings to %s", qPrintable(s.fileName()));
    }
}

} // namespace bifrons
