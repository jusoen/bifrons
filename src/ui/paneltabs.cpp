#include "ui/paneltabs.h"

#include <QCoreApplication>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QTabBar>
#include <QTimer>

#include <algorithm>

#include "core/localfilesource.h"
#include "ui/filepanel.h"

namespace dcpp {

namespace {
constexpr int DragOutMargin = 20;
constexpr const char *TabMimeType = "application/x-dcpp-tab";
} // namespace

QPointer<PanelTabs> PanelTabs::s_dragSource;
QPointer<FilePanel> PanelTabs::s_dragPanel;

PanelTabs::PanelTabs(std::shared_ptr<const FileSource> source, QWidget *parent)
    : QTabWidget(parent)
    , m_source(std::move(source))
{
    setDocumentMode(true);
    setMovable(true);
    setTabsClosable(false);
    setTabBarAutoHide(true);
    tabBar()->setFocusPolicy(Qt::NoFocus);
    tabBar()->setExpanding(false);
    tabBar()->setStyleSheet(QStringLiteral(
        "QTabBar::tab { padding: 4px 12px; margin-right: 2px; border: 1px solid palette(mid);"
        "  border-bottom: 3px solid transparent; border-top-left-radius: 3px; border-top-right-radius: 3px;"
        "  background: palette(window); color: palette(mid); }"
        "QTabBar::tab:hover:!selected { color: palette(window-text); }"
        "QTabBar::tab:selected { font-weight: bold; background: palette(base); color: palette(window-text);"
        "  border-bottom: 3px solid palette(highlight); }"));

    connect(this, &QTabWidget::currentChanged, this, [this](int) {
        FilePanel *panel = currentPanel();
        if (panel != nullptr) {
            panel->focusView();
            emit panelActivated(panel);
        }
    });
    tabBar()->installEventFilter(this);
    setAcceptDrops(true);
}

bool PanelTabs::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != tabBar()) {
        return QTabWidget::eventFilter(watched, event);
    }
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        const int index = mouse->button() == Qt::LeftButton ? tabBar()->tabAt(mouse->position().toPoint()) : -1;
        // Track the panel, not the index: the bar may reorder tabs before the drag leaves it.
        m_pressPanel = index >= 0 ? qobject_cast<FilePanel *>(widget(index)) : nullptr;
        m_pressPos = mouse->position().toPoint();
        break;
    }
    case QEvent::MouseMove: {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        const QRect zone = tabBar()->rect().adjusted(-DragOutMargin, -DragOutMargin, DragOutMargin, DragOutMargin);
        if (m_pressPanel && (mouse->buttons() & Qt::LeftButton) && !zone.contains(mouse->position().toPoint())) {
            startTabDrag(m_pressPanel);
            return true;
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        m_pressPanel = nullptr;
        // Middle-click closes a tab, as in DC and browsers.
        if (mouse->button() == Qt::MiddleButton) {
            closeTab(tabBar()->tabAt(mouse->position().toPoint()));
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QTabWidget::eventFilter(watched, event);
}

void PanelTabs::startTabDrag(FilePanel *panel)
{
    m_pressPanel = nullptr;
    // End the tab bar's own in-bar move first, or it stays stuck mid-drag.
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(m_pressPos), tabBar()->mapToGlobal(QPointF(m_pressPos)),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(tabBar(), &release);
    const int index = indexOf(panel);
    if (index < 0) {
        return;
    }

    auto *mime = new QMimeData;
    mime->setData(QLatin1String(TabMimeType), QByteArray());
    QDrag drag(this);
    drag.setMimeData(mime);
    drag.setPixmap(tabBar()->grab(tabBar()->tabRect(index)));
    s_dragSource = this;
    s_dragPanel = panel;
    (void)drag.exec(Qt::MoveAction);
    s_dragSource = nullptr;
    s_dragPanel = nullptr;
}

bool PanelTabs::acceptsTabDrop(const QMimeData *mime) const
{
    return mime->hasFormat(QLatin1String(TabMimeType)) && s_dragSource != nullptr && s_dragSource != this
        && s_dragPanel != nullptr && s_dragSource->indexOf(s_dragPanel) >= 0;
}

void PanelTabs::dragEnterEvent(QDragEnterEvent *event)
{
    if (acceptsTabDrop(event->mimeData())) {
        event->acceptProposedAction();
        return;
    }
    QTabWidget::dragEnterEvent(event);
}

void PanelTabs::dragMoveEvent(QDragMoveEvent *event)
{
    if (acceptsTabDrop(event->mimeData())) {
        event->acceptProposedAction();
        return;
    }
    QTabWidget::dragMoveEvent(event);
}

void PanelTabs::dropEvent(QDropEvent *event)
{
    if (!acceptsTabDrop(event->mimeData())) {
        QTabWidget::dropEvent(event);
        return;
    }
    int index = -1;
    if (tabBar()->isVisible()) {
        const QPoint pos = tabBar()->mapFrom(this, event->position().toPoint());
        index = tabBar()->tabAt(pos);
        // The right half of a tab means "after it".
        if (index >= 0 && pos.x() > tabBar()->tabRect(index).center().x()) {
            ++index;
        }
    }
    if (index < 0) {
        index = count();
    }
    s_dragSource->moveTabTo(s_dragPanel, this, index);
    event->acceptProposedAction();
}

void PanelTabs::moveTabTo(FilePanel *panel, PanelTabs *target, int index)
{
    const int from = indexOf(panel);
    if (from < 0 || target == nullptr || target == this) {
        return;
    }
    // A side never ends up empty: keep a copy of the last tab here.
    if (count() == 1) {
        addPanel(1, {panel->persistentPath(), panel->sortColumn(), panel->sortOrder()});
    }
    removeTab(indexOf(panel));
    disconnect(panel, nullptr, this, nullptr);
    target->adoptPanel(panel, index);
    target->setCurrentWidget(panel);
    // Windows restores focus to the drag source when the drag ends, so re-assert it afterwards.
    QTimer::singleShot(0, panel, [target, panel] {
        panel->focusView();
        emit target->panelActivated(panel);
    });
}

void PanelTabs::adoptPanel(FilePanel *panel, int index)
{
    panel->setShowHidden(m_showHidden);
    connect(panel, &FilePanel::activated, this, &PanelTabs::panelActivated);
    connect(panel, &FilePanel::switchPanelRequested, this, &PanelTabs::switchPanelRequested);
    connect(panel, &FilePanel::pathChanged, this, [this, panel] { updateTitle(panel); });
    connect(panel, &FilePanel::currentItemChanged, this, [this, panel] { emit currentItemChanged(panel); });
    insertTab(std::clamp(index, 0, count()), panel, QString());
    updateTitle(panel);
}

FilePanel *PanelTabs::currentPanel() const
{
    return qobject_cast<FilePanel *>(currentWidget());
}

QList<FilePanel *> PanelTabs::panels() const
{
    QList<FilePanel *> list;
    for (int i = 0; i < count(); ++i) {
        if (auto *panel = qobject_cast<FilePanel *>(widget(i))) {
            list.append(panel);
        }
    }
    return list;
}

void PanelTabs::newTab()
{
    const FilePanel *current = currentPanel();
    TabState state;
    if (current != nullptr) {
        state.path = current->path();
        state.sortColumn = current->sortColumn();
        state.sortOrder = current->sortOrder();
    }
    FilePanel *panel = addPanel(currentIndex() + 1, state);
    setCurrentWidget(panel);
}

void PanelTabs::closeCurrentTab()
{
    closeTab(currentIndex());
}

void PanelTabs::closeTab(int index)
{
    if (count() <= 1 || index < 0 || index >= count()) {
        return;
    }
    QWidget *page = widget(index);
    removeTab(index);
    page->deleteLater();
}

void PanelTabs::nextTab()
{
    if (count() > 1) {
        setCurrentIndex((currentIndex() + 1) % count());
    }
}

void PanelTabs::previousTab()
{
    if (count() > 1) {
        setCurrentIndex((currentIndex() + count() - 1) % count());
    }
}

void PanelTabs::setShowHidden(bool show)
{
    m_showHidden = show;
    for (FilePanel *panel : panels()) {
        panel->setShowHidden(show);
    }
}

QList<TabState> PanelTabs::state() const
{
    QList<TabState> tabs;
    for (const FilePanel *panel : panels()) {
        tabs.append({panel->persistentPath(), panel->sortColumn(), panel->sortOrder()});
    }
    return tabs;
}

void PanelTabs::restore(const QList<TabState> &tabs, int currentIndex)
{
    while (count() > 0) {
        QWidget *page = widget(0);
        removeTab(0);
        page->deleteLater();
    }
    if (tabs.isEmpty()) {
        addPanel(0, TabState{});
    } else {
        for (const TabState &tab : tabs) {
            addPanel(count(), tab);
        }
    }
    setCurrentIndex(std::clamp(currentIndex, 0, count() - 1));
}

FilePanel *PanelTabs::addPanel(int index, const TabState &state)
{
    auto *panel = new FilePanel(m_source, this);
    panel->setSort(state.sortColumn, state.sortOrder);
    panel->setShowHidden(m_showHidden);
    adoptPanel(panel, index);
    panel->setPath(LocalFileSource::nearestExistingDir(state.path));
    return panel;
}

void PanelTabs::updateTitle(FilePanel *panel)
{
    const int index = indexOf(panel);
    if (index < 0) {
        return;
    }
    if (panel->isShowingResults()) {
        setTabText(index, tr("Search results"));
        setTabToolTip(index, QDir::toNativeSeparators(panel->persistentPath()));
        return;
    }
    const QString name = QDir(panel->path()).dirName();
    // A drive root has no folder name, so show the root itself ("C:").
    const QString title = name.isEmpty() ? QDir::toNativeSeparators(panel->path()).remove(QLatin1Char('\\')) : name;
    setTabText(index, title);
    setTabToolTip(index, QDir::toNativeSeparators(panel->path()));
}

} // namespace dcpp
