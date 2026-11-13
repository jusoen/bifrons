#include "ui/mainwindow.h"

#include <QCheckBox>
#include <QClipboard>
#include <QMimeData>
#include <QtEndian>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QDir>
#include <QFrame>
#include <QInputDialog>
#include <QLabel>
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
#include <QActionGroup>
#include <QStyle>
#include <QStyleHints>
#include <QPainter>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>
#include <limits>
#include <tuple>

#include "core/appinfo.h"
#include "core/localfilesource.h"
#include "core/searchresultfilesource.h"
#include "core/transfer.h"
#include "ui/commandregistry.h"
#if defined(BIFRONS_DEMO_OPERATION)
#include "ui/demooperation.h"
#endif
#include "ui/filepanel.h"
#include "ui/hotkeysdialog.h"
#include "ui/transferdialog.h"
#include "ui/favouritesdialog.h"
#include "ui/finddialog.h"
#include "ui/multirenamedialog.h"
#include "ui/operationqueue.h"
#include "ui/paneltabs.h"
#include "ui/paneltoolbar.h"
#include "ui/shellmenu.h"
#include "ui/terminal.h"
#include "ui/usertooldialog.h"
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

// Settings value for each theme. Anything unrecognised reads as following Windows.
QString themeKey(Qt::ColorScheme scheme)
{
    switch (scheme) {
    case Qt::ColorScheme::Light:
        return QStringLiteral("light");
    case Qt::ColorScheme::Dark:
        return QStringLiteral("dark");
    case Qt::ColorScheme::Unknown:
        break;
    }
    return QStringLiteral("system");
}

Qt::ColorScheme themeFromKey(const QString &key)
{
    if (key == QLatin1String("light")) {
        return Qt::ColorScheme::Light;
    }
    if (key == QLatin1String("dark")) {
        return Qt::ColorScheme::Dark;
    }
    return Qt::ColorScheme::Unknown;
}

// Windows clipboard format that tells Explorer whether pasted files are copied or moved.
const QString PreferredDropEffectMime = QStringLiteral("application/x-qt-windows-mime;value=\"Preferred DropEffect\"");
constexpr quint32 DropEffectCopy = 1;
constexpr quint32 DropEffectMove = 2;
constexpr int PaneBarButtonSize = 26;
constexpr int PaneBarIconSize = 20;
constexpr int ToastMs = 500;

// Direction arrow, with the drag cursor's plus box beneath it for a copy. Drawn at native size so 1 px lines stay sharp.
QIcon transferIcon(bool copy, bool toRight, const QColor &colour, qreal dpr)
{
    constexpr qreal Size = PaneBarIconSize;
    QPixmap pixmap(qRound(Size * dpr), qRound(Size * dpr));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    if (!toRight) {
        painter.translate(Size, 0);
        painter.scale(-1, 1);
    }
    QPen pen(colour, 1.0);
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
    // A copy raises the arrow to make room for the box beneath it.
    const qreal y = copy ? 5.5 : 10.5;
    painter.drawLine(QPointF(2, y), QPointF(17.5, y));
    painter.drawPolyline(QPolygonF({QPointF(13, y - 4.5), QPointF(17.5, y), QPointF(13, y + 4.5)}));
    if (copy) {
        painter.drawRect(QRectF(6.5, 11.5, 8, 8));
        painter.drawLine(QPointF(10.5, 13), QPointF(10.5, 18));
        painter.drawLine(QPointF(8, 15.5), QPointF(13, 15.5));
    }
    painter.end();
    return QIcon(pixmap);
}

// True when the clipboard holds a cut from Explorer: its Preferred DropEffect is move without copy.
bool clipboardIsCut(const QMimeData *mime)
{
    const QByteArray data = mime->data(PreferredDropEffectMime);
    if (data.size() < static_cast<qsizetype>(sizeof(quint32))) {
        return false;
    }
    const quint32 effect = qFromLittleEndian<quint32>(data.constData());
    return (effect & DropEffectMove) != 0 && (effect & DropEffectCopy) == 0;
}

// The gap between the button pairs. Dragging it slides the pane bar along the divider.
class PaneBarGrip : public QWidget
{
public:
    using QWidget::QWidget;

    std::function<void(int startCentreY, int dy)> onDrag;
    std::function<int()> barCentreY;
    std::function<void()> onDoubleClick;

protected:
    void paintEvent(QPaintEvent *) override
    {
        // Whole-pixel squares, not antialiased circles, so the dots stay sharp.
        QPainter painter(this);
        const int cx = width() / 2;
        const int cy = height() / 2;
        for (int i = -1; i <= 1; ++i) {
            painter.fillRect(cx - 1 + i * 4, cy - 1, 2, 2, palette().color(QPalette::Mid));
        }
    }

    void enterEvent(QEnterEvent *) override
    {
        setCursor(Qt::SizeVerCursor);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !barCentreY) {
            QWidget::mousePressEvent(event);
            return;
        }
        m_pressY = event->globalPosition().toPoint().y();
        m_startCentreY = barCentreY();
        m_dragging = true;
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_dragging && onDrag) {
            onDrag(m_startCentreY, event->globalPosition().toPoint().y() - m_pressY);
        }
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        m_dragging = false;
        event->accept();
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && onDoubleClick) {
            onDoubleClick();
        }
        event->accept();
    }

private:
    bool m_dragging = false;
    int m_pressY = 0;
    int m_startCentreY = 0;
};

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_source(std::make_shared<LocalFileSource>())
{
    // Applied before any widget exists, so nothing is built in the wrong colours.
    m_theme = themeFromKey(settingsStore().value(QStringLiteral("view/theme")).toString());
    QGuiApplication::styleHints()->setColorScheme(m_theme);

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
        connect(side, &PanelTabs::filesDropped, this, &MainWindow::dropFiles);
        connect(side, &PanelTabs::renameRequested, this, &MainWindow::renameInPlace);
        // A new tab's list is watched from here on, and may sit a little differently.
        connect(side, &PanelTabs::currentPanelChanged, this, &MainWindow::placePaneBar);
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
    m_commands->captureDefaults();
    m_commands->refreshCaptions();
    {
        QSettings s = settingsStore();
        m_commands->applyHotkeys(loadHotkeys(s));
    }
    const QHash<QString, QStringList> clashes = findClashes(m_commands->currentHotkeys());
    for (auto it = clashes.constBegin(); it != clashes.constEnd(); ++it) {
        // Qt fires neither command for a shared key, so name them in the debug output.
        qWarning("Hot key %s is bound to %s", qPrintable(it.key()),
                 qPrintable(it.value().join(QStringLiteral(", "))));
    }
    createMenus();
    createPaneBar();

    setWindowTitle(QStringLiteral("%1 %2").arg(appDisplayName(), appVersion()));
    statusBar()->hide();
    resize(1100, 700);
    loadSettings();
}

void MainWindow::createCommands()
{
    m_commands = new CommandRegistry(this);
    m_commands->add(QStringLiteral("cm_Copy"), tr("&Copy"), Qt::Key_F5, [this] { cmCopy(); });
    m_commands->add(QStringLiteral("cm_Rename"), tr("&Move"), Qt::Key_F6, [this] { cmRename(); });
    m_commands->add(QStringLiteral("cm_RenameOnly"), tr("&Rename"), Qt::SHIFT | Qt::Key_F6,
                    [this] { cmRenameOnly(); });
    m_commands->add(QStringLiteral("cm_MakeDir"), tr("&New Folder"), Qt::Key_F7, [this] { cmMakeDir(); });
    // Del as well as DC's F8. A focused text field still gets Del, because it claims the key first.
    m_commands->add(QStringLiteral("cm_CutToClipboard"), tr("Cu&t"), QKeySequence(Qt::CTRL | Qt::Key_X),
                    [this] { copyToClipboard(true); });
    m_commands->add(QStringLiteral("cm_CopyToClipboard"), tr("C&opy"), QKeySequence(Qt::CTRL | Qt::Key_C),
                    [this] { copyToClipboard(false); });
    m_commands->add(QStringLiteral("cm_PasteFromClipboard"), tr("&Paste"), QKeySequence(Qt::CTRL | Qt::Key_V),
                    [this] { cmPaste(); });
    // Driven by the bar in the splitter. They act at once on the source side's selection, without a dialog.
    m_commands->add(QStringLiteral("cm_CopyToRight"), tr("Copy to Right Pane"), QKeySequence(),
                    [this] { transferAcross(m_left, false); });
    m_commands->add(QStringLiteral("cm_CopyToLeft"), tr("Copy to Left Pane"), QKeySequence(),
                    [this] { transferAcross(m_right, false); });
    m_commands->add(QStringLiteral("cm_MoveToRight"), tr("Move to Right Pane"), QKeySequence(),
                    [this] { transferAcross(m_left, true); });
    m_commands->add(QStringLiteral("cm_MoveToLeft"), tr("Move to Left Pane"), QKeySequence(),
                    [this] { transferAcross(m_right, true); });
    m_commands->add(QStringLiteral("cm_Delete"), tr("&Delete"), Qt::Key_F8, [this] { cmDelete(); })
        ->setShortcuts({QKeySequence(Qt::Key_F8), QKeySequence(Qt::Key_Delete)});
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
    m_commands->add(QStringLiteral("cm_ShowPaneBar"), tr("Show Copy/Move &Buttons"), QKeySequence(),
                    [this] { setShowPaneBar(m_paneBar != nullptr && m_paneBar->isHidden()); })
        ->setCheckable(true);
    m_commands->add(QStringLiteral("cm_ShowFolderSizes"), tr("Show &Folder Sizes"), QKeySequence(),
                    [this] { setShowFolderSizes(!m_showFolderSizes); })
        ->setCheckable(true);
    QAction *hidden = m_commands->add(QStringLiteral("cm_ShowSysFiles"), tr("Show &Hidden Files"),
                                      Qt::CTRL | Qt::Key_Period, [this] { setShowHidden(!m_showHidden); });
    hidden->setCheckable(true);
    auto *themes = new QActionGroup(this);
    for (const auto &[name, caption, scheme] :
         {std::tuple{"cm_ThemeSystem", tr("&System"), Qt::ColorScheme::Unknown},
          std::tuple{"cm_ThemeLight", tr("&Light"), Qt::ColorScheme::Light},
          std::tuple{"cm_ThemeDark", tr("&Dark"), Qt::ColorScheme::Dark}}) {
        const Qt::ColorScheme chosen = scheme;
        QAction *action = m_commands->add(QLatin1String(name), caption, QKeySequence(),
                                          [this, chosen] { setTheme(chosen); });
        action->setCheckable(true);
        action->setChecked(chosen == m_theme);
        themes->addAction(action);
    }
    // + and - stay panel keys (a window shortcut would steal them from text fields), so no shortcut here.
    m_commands->add(QStringLiteral("cm_MarkPlus"), tr("&Select a Group\t+"), QKeySequence(),
                    [this] { m_active->markByMaskPrompt(true); });
    m_commands->add(QStringLiteral("cm_MarkMinus"), tr("&Unselect a Group\t-"), QKeySequence(),
                    [this] { m_active->markByMaskPrompt(false); });
    m_commands->add(QStringLiteral("cm_MarkMarkAll"), tr("Select &All\tCtrl+A"), QKeySequence(),
                    [this] { m_active->markAll(true); });
    m_commands->add(QStringLiteral("cm_MarkUnmarkAll"), tr("U&nselect All"), QKeySequence(),
                    [this] { m_active->markAll(false); });
    m_commands->add(QStringLiteral("cm_View"), tr("&View File"), Qt::Key_F3, [this] { cmView(); });
    m_commands->add(QStringLiteral("cm_QuickView"), tr("&Quick View"), Qt::CTRL | Qt::Key_Q,
                    [this] { cmQuickView(); });
    m_commands->action(QStringLiteral("cm_QuickView"))->setCheckable(true);
    m_commands->add(QStringLiteral("cm_ConfigHotKeys"), tr("Hot &Keys"), QKeySequence(), [this] { configHotKeys(); });
    m_commands->add(QStringLiteral("cm_ConfirmDelete"), tr("Confirm D&elete"), QKeySequence(), [this] {
        setConfirmDelete(!m_confirmDelete);
        settingsStore().setValue(QStringLiteral("operations/confirmDelete"), m_confirmDelete);
    })->setCheckable(true);
    m_commands->add(QStringLiteral("cm_ConfirmDragDrop"), tr("&Confirm Drag and Drop"), QKeySequence(), [this] {
        setConfirmDrop(!m_confirmDrop);
        settingsStore().setValue(QStringLiteral("operations/confirmDrop"), m_confirmDrop);
    })->setCheckable(true);
    m_commands->add(QStringLiteral("cm_QuickFilter"), tr("Q&uick Filter"), Qt::CTRL | Qt::Key_F,
                    [this] { m_active->showQuickFilter(); });
    m_commands->add(QStringLiteral("cm_DirHotList"), tr("&Favourites"), Qt::CTRL | Qt::Key_D, [this] {
        QMenu menu(this);
        fillFavouritesMenu(&menu);
        menu.exec(m_active->mapToGlobal(QPoint(0, 0)));
    });
    m_commands->add(QStringLiteral("cm_About"), tr("&About %1").arg(appDisplayName()), QKeySequence(), [this] {
        QMessageBox::about(this, tr("About %1").arg(appDisplayName()),
                           tr("%1 %2\n\nA dual-pane file manager inspired by Double Commander.\n\nCopyright © 2026 Julian Mitchell\nMIT License.")
                               .arg(appDisplayName(), appVersion()));
    });
    m_commands->add(QStringLiteral("cm_MultiRename"), tr("Multi-Rena&me"), Qt::CTRL | Qt::Key_M,
                    [this] { cmMultiRename(); });
    m_commands->add(QStringLiteral("cm_Search"), tr("&Find Files"), Qt::ALT | Qt::Key_F7, [this] { cmSearch(); });
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
    addMenu(tr("&File"), {"cm_View", nullptr, "cm_Copy", "cm_Rename", "cm_RenameOnly", "cm_MakeDir", "cm_Delete",
                          nullptr, "cm_CutToClipboard", "cm_CopyToClipboard", "cm_PasteFromClipboard", nullptr,
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
    QMenu *view = addMenu(tr("&View"), {"cm_QuickView", "cm_QuickFilter", nullptr, "cm_ShowSysFiles", "cm_ShowFolderSizes",
                                          "cm_ShowPaneBar", nullptr, "cm_ConfirmDragDrop", "cm_ConfirmDelete"});
    view->addSeparator();
    QMenu *theme = view->addMenu(tr("&Theme"));
    for (const char *name : {"cm_ThemeSystem", "cm_ThemeLight", "cm_ThemeDark"}) {
        theme->addAction(m_commands->action(QLatin1String(name)));
    }
    addMenu(tr("Confi&guration"), {"cm_ConfigHotKeys"});
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
    connect(menu->addAction(tr("&Organise favourites")), &QAction::triggered, this, [this] {
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

void MainWindow::setTheme(Qt::ColorScheme scheme)
{
    m_theme = scheme;
    QGuiApplication::styleHints()->setColorScheme(scheme);
    // Saved straight away, like the user tools, so the choice survives a crash.
    settingsStore().setValue(QStringLiteral("view/theme"), themeKey(scheme));
}

void MainWindow::changeEvent(QEvent *event)
{
    // Deferred because children receive the new palette after this window does.
    if (event->type() == QEvent::PaletteChange && m_left != nullptr) {
        QMetaObject::invokeMethod(this, &MainWindow::refreshThemeColours, Qt::QueuedConnection);
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::refreshThemeColours()
{
    for (PanelTabs *side : {m_left, m_right}) {
        side->refreshColours();
    }
    for (PanelToolbar *toolbar : std::as_const(m_toolbars)) {
        toolbar->refreshIcons();
    }
    refreshPaneBarIcons();
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
    m_toolbars.insert(side, toolbar);
    connect(toolbar, &PanelToolbar::userToolClicked, this, [this, side](int index) { runUserTool(index, side); });
    connect(toolbar, &PanelToolbar::userToolEditRequested, this, &MainWindow::editUserTool);
    connect(toolbar, &PanelToolbar::userToolRemoveRequested, this, [this](int index) {
        if (index >= 0 && index < m_userTools.size()) {
            UserToolList tools = m_userTools;
            tools.removeAt(index);
            setUserTools(tools);
        }
    });
    connect(toolbar, &PanelToolbar::userToolMoveRequested, this, [this](int from, int to) {
        if (from >= 0 && to >= 0 && from < m_userTools.size() && to < m_userTools.size()) {
            UserToolList tools = m_userTools;
            tools.move(from, to);
            setUserTools(tools);
        }
    });
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

void MainWindow::cmPaste()
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (mime == nullptr || !mime->hasUrls()) {
        return;
    }
    QStringList sources;
    for (const QUrl &url : mime->urls()) {
        if (!url.isLocalFile()) {
            QMessageBox::information(this, tr("Paste"), tr("Only files and folders can be pasted."));
            return;
        }
        sources.append(QDir::fromNativeSeparators(url.toLocalFile()));
    }
    const bool move = clipboardIsCut(mime);
    if (transferInto(m_active, sources, move, tr("Paste")) && move) {
        // As in Explorer, a cut pastes once. The originals are on their way to this folder.
        QGuiApplication::clipboard()->clear();
    }
}

bool MainWindow::transferInto(FilePanel *target, const QStringList &sources, bool move, const QString &title)
{
    if (target == nullptr || sources.isEmpty()) {
        return false;
    }
    const QString problem = target->pasteProblem(sources);
    if (!problem.isEmpty()) {
        QMessageBox::information(this, title, problem);
        return false;
    }
    // The trailing slash makes planTransfer put every item inside the folder.
    const QString folder = target->path();
    const TransferPlan plan = planTransfer(sources, folder + QLatin1Char('/'), folder);
    if (!plan.ok) {
        QMessageBox::warning(this, title, plan.error);
        return false;
    }
    if (move) {
        releaseFolders(sources);
    }
    runOperation(new LocalFileOperation(move ? LocalFileOperation::Kind::Move : LocalFileOperation::Kind::Copy,
                                        plan.items),
                 move ? tr("Move") : tr("Copy"));
    return true;
}

void MainWindow::copyToClipboard(bool cut)
{
    const QStringList paths = m_active->selectedPaths();
    if (paths.isEmpty()) {
        return;
    }
    QList<QUrl> urls;
    urls.reserve(paths.size());
    for (const QString &path : paths) {
        urls.append(QUrl::fromLocalFile(path));
    }
    QByteArray effect(static_cast<qsizetype>(sizeof(quint32)), '\0');
    qToLittleEndian<quint32>(cut ? DropEffectMove : DropEffectCopy, effect.data());
    auto *mime = new QMimeData;
    mime->setUrls(urls);
    mime->setData(PreferredDropEffectMime, effect);
    // The clipboard takes ownership of mime.
    QGuiApplication::clipboard()->setMimeData(mime);
}

void MainWindow::showToast(const QString &text, FilePanel *panel, bool leftOfBar)
{
    QWidget *parent = m_splitter->parentWidget();
    if (m_toast == nullptr) {
        m_toast = new QLabel(parent);
        m_toast->setStyleSheet(QStringLiteral(
            "QLabel { background: palette(tool-tip-base); color: palette(tool-tip-text);"
            " border: 1px solid palette(mid); border-radius: 4px; padding: 4px 8px; }"));
        m_toastTimer.setSingleShot(true);
        m_toastTimer.setInterval(ToastMs);
        connect(&m_toastTimer, &QTimer::timeout, m_toast, &QWidget::hide);
    }
    m_toast->setText(text);
    m_toast->adjustSize();
    QPoint at;
    if (m_paneBar != nullptr && m_paneBar->isVisible()) {
        // Beside the bar, on the side the files would have come from.
        const QRect bar = m_paneBar->geometry();
        const int x = leftOfBar ? bar.left() - m_toast->width() - 4 : bar.right() + 5;
        at = QPoint(x, bar.center().y() - m_toast->height() / 2);
    } else if (panel != nullptr) {
        const QRect area(panel->mapTo(parent, QPoint(0, 0)), panel->size());
        at = area.center() - QPoint(m_toast->width() / 2, m_toast->height() / 2);
    }
    m_toast->move(at);
    m_toast->show();
    m_toast->raise();
    m_toastTimer.start();
}

void MainWindow::transferAcross(PanelTabs *from, bool move)
{
    // A double-click on a bar button arrives as two clicks. Only the first one counts.
    if (m_lastAcross.isValid() && m_lastAcross.elapsed() < QGuiApplication::styleHints()->mouseDoubleClickInterval()) {
        return;
    }
    m_lastAcross.start();
    FilePanel *source = from->currentPanel();
    FilePanel *target = (from == m_left ? m_right : m_left)->currentPanel();
    if (source == nullptr || target == nullptr) {
        return;
    }
    const QStringList sources = source->selectedPaths();
    if (sources.isEmpty()) {
        showToast(tr("Nothing selected"), source, from == m_left);
        return;
    }
    // The source side runs the operation, as with F5 and F6.
    setActivePanel(source);
    source->focusView();
    (void)transferInto(target, sources, move, move ? tr("Move") : tr("Copy"));
}

void MainWindow::createPaneBar()
{
    QSplitterHandle *handle = m_splitter->handle(1);
    if (handle == nullptr) {
        return;
    }
    // Floats over the handle. A splitter child would become a third pane.
    m_paneBar = new QFrame(m_splitter->parentWidget());
    m_paneBar->setObjectName(QStringLiteral("paneBar"));
    m_paneBar->setStyleSheet(QStringLiteral(
        "QFrame#paneBar { background: palette(window); border: 1px solid palette(mid); border-radius: 4px; }"));
    auto *layout = new QVBoxLayout(m_paneBar);
    layout->setContentsMargins(2, 3, 2, 3);
    layout->setSpacing(2);
    const auto addButton = [this, layout](const char *command, bool copy, bool toRight) {
        QAction *action = m_commands->action(QLatin1String(command));
        Q_ASSERT(action != nullptr);
        auto *button = new QToolButton(m_paneBar);
        button->setToolTip(action->text().remove(QLatin1Char('&')));
        button->setFixedSize(PaneBarButtonSize, PaneBarButtonSize);
        button->setIconSize(QSize(PaneBarIconSize, PaneBarIconSize));
        button->setAutoRaise(true);
        // Clicking leaves the keyboard focus in the panel.
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QToolButton::clicked, action, &QAction::trigger);
        layout->addWidget(button);
        m_paneButtons.append({button, copy, toRight});
    };
    addButton("cm_CopyToRight", true, true);
    addButton("cm_MoveToRight", false, true);
    auto *grip = new PaneBarGrip(m_paneBar);
    grip->setFixedSize(PaneBarButtonSize, PaneBarButtonSize / 2);
    grip->setToolTip(tr("Drag to move the buttons along the divider, double-click to centre them"));
    grip->onDrag = [this](int startCentreY, int dy) { dragPaneBar(startCentreY, dy); };
    grip->barCentreY = [this] { return m_paneBar->geometry().center().y(); };
    grip->onDoubleClick = [this] {
        m_paneBarPos = 0.5;
        placePaneBar();
    };
    layout->addWidget(grip);
    addButton("cm_CopyToLeft", true, false);
    addButton("cm_MoveToLeft", false, false);
    m_paneBar->adjustSize();
    refreshPaneBarIcons();
    // The handle moves on drags, window resizes and restored layouts alike.
    handle->installEventFilter(this);
    placePaneBar();
}

QPair<int, int> MainWindow::paneBarBounds()
{
    // The file lists both sides show, so the bar never covers a toolbar, tab row or status line.
    QWidget *parent = m_paneBar->parentWidget();
    int top = std::numeric_limits<int>::min();
    int bottom = std::numeric_limits<int>::max();
    for (PanelTabs *side : {m_left, m_right}) {
        FilePanel *panel = side->currentPanel();
        QWidget *list = panel != nullptr ? panel->listView() : nullptr;
        if (list == nullptr || !list->isVisible()) {
            continue;
        }
        // Follows the list as it resizes, for example when the quick filter opens.
        list->installEventFilter(this);
        const QPoint origin = list->mapTo(parent, QPoint(0, 0));
        top = std::max(top, origin.y());
        bottom = std::min(bottom, origin.y() + list->height());
    }
    if (top == std::numeric_limits<int>::min()) {
        QSplitterHandle *handle = m_splitter->handle(1);
        top = handle->mapTo(parent, QPoint(0, 0)).y();
        bottom = top + handle->height();
    }
    return {top, bottom};
}

void MainWindow::placePaneBar()
{
    QSplitterHandle *handle = m_splitter->handle(1);
    if (m_paneBar == nullptr || handle == nullptr) {
        return;
    }
    const auto [top, bottom] = paneBarBounds();
    const int travel = bottom - top - m_paneBar->height();
    // Too short a range (before the first layout) just centres the bar in it.
    const int y = travel <= 0 ? top + travel / 2 : top + qRound(m_paneBarPos * travel);
    const int x = handle->mapTo(m_paneBar->parentWidget(), QPoint(handle->width() / 2, 0)).x();
    m_paneBar->move(x - m_paneBar->width() / 2, y);
    m_paneBar->raise();
}

void MainWindow::dragPaneBar(int startCentreY, int dy)
{
    if (m_paneBar == nullptr || m_splitter->handle(1) == nullptr) {
        return;
    }
    const auto [top, bottom] = paneBarBounds();
    const int travel = bottom - top - m_paneBar->height();
    if (travel <= 0) {
        return;
    }
    const int barTop = startCentreY + dy - m_paneBar->height() / 2;
    m_paneBarPos = qBound(0.0, static_cast<qreal>(barTop - top) / travel, 1.0);
    placePaneBar();
}

void MainWindow::setShowPaneBar(bool show)
{
    if (m_paneBar != nullptr) {
        m_paneBar->setVisible(show);
    }
    m_commands->action(QStringLiteral("cm_ShowPaneBar"))->setChecked(show);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Only the splitter handle and the file lists the pane bar sits beside are watched.
    const QEvent::Type type = event->type();
    if (type == QEvent::Move || type == QEvent::Resize || type == QEvent::Show || type == QEvent::Hide) {
        placePaneBar();
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::refreshPaneBarIcons()
{
    for (const PaneBarButton &entry : std::as_const(m_paneButtons)) {
        entry.button->setIcon(
            transferIcon(entry.copy, entry.toRight, entry.button->palette().color(QPalette::ButtonText),
                         entry.button->devicePixelRatioF()));
    }
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

void MainWindow::dropFiles(FilePanel *panel, const QStringList &sources, const QString &targetDir, bool move)
{
    if (panel == nullptr || sources.isEmpty()) {
        return;
    }
    // The drop side runs the operation, so it shows the progress.
    setActivePanel(panel);
    panel->focusView();
    activateWindow();
    const QString baseDir = QFileInfo(sources.first()).absolutePath();
    if (move) {
        transfer(LocalFileOperation::Kind::Move, sources, baseDir, tr("Move"),
                 tr("Move %1 item(s) to:").arg(sources.size()), m_source->displayPath(targetDir), true);
    } else {
        transfer(LocalFileOperation::Kind::Copy, sources, baseDir, tr("Copy"),
                 tr("Copy %1 item(s) to:").arg(sources.size()), m_source->displayPath(targetDir), true);
    }
}

void MainWindow::configHotKeys()
{
    QList<HotkeysDialog::Command> commands;
    for (const QString &name : m_commands->names()) {
        // "&Select a Group\t+" reads as "Select a Group" in the list.
        QString caption = m_commands->action(name)->text().section(QLatin1Char('\t'), 0, 0);
        caption.remove(QLatin1Char('&'));
        commands.append({name, caption});
    }
    HotkeysDialog dialog(commands, m_commands->currentHotkeys(), m_commands->defaultHotkeys(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const HotkeyMap map = dialog.hotkeys();
    m_commands->applyHotkeys(map);
    QSettings s = settingsStore();
    saveHotkeys(s, map, m_commands->defaultHotkeys());
}

void MainWindow::setConfirmDelete(bool confirm)
{
    m_confirmDelete = confirm;
    m_commands->action(QStringLiteral("cm_ConfirmDelete"))->setChecked(confirm);
}

void MainWindow::setConfirmDrop(bool confirm)
{
    m_confirmDrop = confirm;
    m_commands->action(QStringLiteral("cm_ConfirmDragDrop"))->setChecked(confirm);
}

void MainWindow::renameInPlace(FilePanel *panel, const QString &path, const QString &newName)
{
    setActivePanel(panel);
    const QString target = QDir(QFileInfo(path).absolutePath()).filePath(newName);
    // Source and target given directly, not through planTransfer, which would move into a folder of that name.
    releaseFolders({path});
    runOperation(new LocalFileOperation(LocalFileOperation::Kind::Move, {{path, target}}), tr("Rename"));
}

void MainWindow::cmRenameOnly()
{
    // Renames only the item under the cursor, whatever is marked. In place where the panel can.
    if (m_active->startInlineRename()) {
        return;
    }
    const QString path = m_active->currentPath();
    if (path.isEmpty()) {
        return;
    }
    const QFileInfo info(path);
    transfer(LocalFileOperation::Kind::Move, {path}, info.absolutePath(), tr("Rename"), tr("New name:"),
             info.fileName());
}

void MainWindow::transfer(LocalFileOperation::Kind kind, const QStringList &sources, const QString &baseDir,
                          const QString &title, const QString &label, const QString &defaultDestination,
                          bool fromDrop)
{
    if (sources.isEmpty()) {
        return;
    }
    QString destination = defaultDestination;
    if (!fromDrop || m_confirmDrop) {
        TransferDialog dialog(title, label, defaultDestination, fromDrop, this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        destination = dialog.destination();
        if (dialog.doNotAskAgain()) {
            setConfirmDrop(false);
            // Saved now, so the choice holds even if Bifrons does not close cleanly.
            settingsStore().setValue(QStringLiteral("operations/confirmDrop"), false);
        }
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
    if (kind == LocalFileOperation::Kind::Move) {
        releaseFolders(sources);
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
    // A share may have no recycle bin, so Windows could delete outright. Those always ask.
    const bool network = std::any_of(paths.cbegin(), paths.cend(),
                                     [](const QString &p) { return LocalFileSource::isNetworkPath(p); });
    if (m_confirmDelete || network) {
        QMessageBox box(QMessageBox::Question, tr("Delete"), question, QMessageBox::Yes | QMessageBox::No, this);
        if (!network) {
            box.setCheckBox(new QCheckBox(tr("Do not ask again"), &box));
        }
        if (box.exec() != QMessageBox::Yes) {
            return;
        }
        if (box.checkBox() != nullptr && box.checkBox()->isChecked()) {
            setConfirmDelete(false);
            settingsStore().setValue(QStringLiteral("operations/confirmDelete"), false);
        }
    }
    QList<TransferItem> items;
    for (const QString &path : paths) {
        items.append({path, QString()});
    }
    releaseFolders(paths);
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

void MainWindow::releaseFolders(const QStringList &paths)
{
    for (PanelTabs *side : {m_left, m_right}) {
        for (FilePanel *panel : side->panels()) {
            panel->releaseFolderUnder(paths);
        }
    }
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

void MainWindow::setUserTools(const UserToolList &tools)
{
    m_userTools = tools;
    for (PanelToolbar *toolbar : std::as_const(m_toolbars)) {
        toolbar->setUserTools(m_userTools);
    }
    // Saved straight away, so a crash does not lose a button that took effort to set up.
    QSettings s = settingsStore();
    saveUserTools(s, m_userTools);
}

void MainWindow::editUserTool(int index)
{
    const bool adding = index < 0 || index >= m_userTools.size();
    UserToolDialog dialog(adding ? UserTool() : m_userTools.at(index), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    UserToolList tools = m_userTools;
    if (adding) {
        tools.append(dialog.tool());
    } else {
        tools[index] = dialog.tool();
    }
    setUserTools(tools);
}

void MainWindow::runUserTool(int index, PanelTabs *side)
{
    FilePanel *panel = side != nullptr ? side->currentPanel() : nullptr;
    if (panel == nullptr || index < 0 || index >= m_userTools.size()) {
        return;
    }
    const FilePanel *other = (side == m_left ? m_right : m_left)->currentPanel();
    ToolContext context;
    context.currentDir = panel->persistentPath();
    context.currentPath = panel->currentPath();
    context.selectedPaths = panel->selectedPaths();
    context.targetDir = other != nullptr ? other->persistentPath() : QString();
    const UserTool &tool = m_userTools.at(index);
    QString error;
    if (!launchProgram(tool.program, expandToolArguments(tool.arguments, context), context.currentDir, &error)) {
        QMessageBox::warning(this, tool.name.isEmpty() ? tr("Toolbar Button") : tool.name, error);
    }
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
    // Menu text with the command's first key after a tab, so it follows any rebinding.
    const auto keyed = [this](const QString &text, const char *name) {
        const QString keys = m_commands->keyText(QLatin1String(name));
        return keys.isEmpty() ? text : text + QLatin1Char('\t') + keys;
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
            {keyed(tr("New &Folder"), "cm_MakeDir"), command("cm_MakeDir")},
            {keyed(tr("Open &Terminal Here"), "cm_RunTerm"), command("cm_RunTerm")},
            {keyed(tr("&Paste"), "cm_PasteFromClipboard"), command("cm_PasteFromClipboard")},
            {keyed(tr("&Refresh"), "cm_Refresh"), command("cm_Refresh")},
            {QString(), {}},
            {tr("Copy Folder &Path"), copyPaths({folder})},
        });
        return;
    }

    const QStringList paths = panel->selectedPaths();
    const QList<MenuEntry> entries = {
        {keyed(tr("&View"), "cm_View"), command("cm_View")},
        {keyed(tr("&Copy to Other Pane"), "cm_Copy"), command("cm_Copy")},
        {keyed(tr("&Move to Other Pane"), "cm_Rename"), command("cm_Rename")},
        {keyed(tr("Re&name"), "cm_RenameOnly"), command("cm_RenameOnly")},
        {keyed(tr("&Delete"), "cm_Delete"), command("cm_Delete")},
        {QString(), {}},
        {keyed(tr("Cu&t"), "cm_CutToClipboard"), command("cm_CutToClipboard")},
        {keyed(tr("C&opy"), "cm_CopyToClipboard"), command("cm_CopyToClipboard")},
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
    for (PanelTabs *side : {m_left, m_right}) {
        for (FilePanel *panel : side->panels()) {
            panel->reloadIfReleased();
        }
    }

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
    // Back to the style's width, in case the saved state carries a wider handle from an earlier build.
    m_splitter->setHandleWidth(-1);
    m_paneBarPos = qBound(0.0, s.value(QStringLiteral("window/paneBarPosition"), 0.5).toDouble(), 1.0);
    placePaneBar();

    setShowHidden(s.value(QStringLiteral("view/showHidden"), false).toBool());
    setShowFolderSizes(s.value(QStringLiteral("view/folderSizes"), false).toBool());
    setShowPaneBar(s.value(QStringLiteral("view/paneBar"), true).toBool());
    setConfirmDrop(s.value(QStringLiteral("operations/confirmDrop"), true).toBool());
    setConfirmDelete(s.value(QStringLiteral("operations/confirmDelete"), true).toBool());
    m_userTools = loadUserTools(s);
    for (PanelToolbar *toolbar : std::as_const(m_toolbars)) {
        toolbar->setUserTools(m_userTools);
    }
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
    s.setValue(QStringLiteral("window/paneBarPosition"), m_paneBarPos);
    s.setValue(QStringLiteral("view/showHidden"), m_showHidden);
    s.setValue(QStringLiteral("view/folderSizes"), m_showFolderSizes);
    s.setValue(QStringLiteral("view/paneBar"), m_paneBar != nullptr && !m_paneBar->isHidden());
    s.setValue(QStringLiteral("operations/confirmDrop"), m_confirmDrop);
    s.setValue(QStringLiteral("operations/confirmDelete"), m_confirmDelete);
    saveUserTools(s, m_userTools);
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
