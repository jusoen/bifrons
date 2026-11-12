#include "ui/paneltabs.h"

#include <QApplication>
#include <QDir>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QTabBar>
#include <QTimer>

#include <algorithm>
#include <cmath>

#include "core/localfilesource.h"
#include "ui/filepanel.h"

namespace bifrons {

namespace {
constexpr int DragOutMargin = 20;
constexpr int DropMarkerWidth = 3;
} // namespace

// Redraws one tab bar during a tab drag: the dragged tab can be taken out, and a gap opens where it
// would drop. Tabs slide to their new places a step per frame, like the bar's own reordering.
class TabGhost : public QWidget
{
public:
    // dragged is the dragged tab's widget when this is the source bar, otherwise nullptr.
    TabGhost(QTabBar *bar, const QWidget *dragged, const QHash<QWidget *, QPixmap> &images,
             const QList<QWidget *> &pages, QWidget *top)
        : QWidget(top)
        , m_bar(bar)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        const int n = bar->count();
        for (int i = 0; i < n; ++i) {
            const QRect r = bar->tabRect(i);
            Item item;
            const QWidget *page = i < pages.size() ? pages.at(i) : nullptr;
            item.image = images.value(const_cast<QWidget *>(page), QPixmap());
            if (item.image.isNull()) {
                item.image = bar->grab(r);
            }
            item.top = r.top();
            item.advance = i + 1 < n ? bar->tabRect(i + 1).left() - r.left() : r.width() + TrailingSpacing;
            item.x = r.left();
            item.target = r.left();
            item.dragged = page != nullptr && page == dragged;
            m_items.append(item);
        }
        m_origin = n > 0 ? bar->tabRect(0).left() : 0;
        setGeometry(QRect(bar->mapTo(top, QPoint(0, 0)), bar->size()));
        m_timer.setInterval(FrameMs);
        QObject::connect(&m_timer, &QTimer::timeout, this, [this] { step(); });
        show();
    }

    // Lays the tabs out with a gap of gapWidth before visible tab gap (-1 for no gap).
    // showDragged keeps the dragged tab in its slot, as for a copy.
    void setLayout(bool showDragged, int gap, int gapWidth)
    {
        int x = m_origin;
        int visible = 0;
        for (Item &item : m_items) {
            item.hidden = item.dragged && !showDragged;
            if (item.hidden) {
                continue;
            }
            if (visible == gap) {
                x += gapWidth;
            }
            item.target = x;
            x += item.advance;
            ++visible;
        }
        m_timer.start();
    }

    // Insertion index among visible tabs for a drop at barX, against the layout without a gap.
    int indexAt(int barX, bool showDragged) const
    {
        int x = m_origin;
        int index = 0;
        for (const Item &item : m_items) {
            if (item.dragged && !showDragged) {
                continue;
            }
            if (barX < x + item.advance / 2) {
                return index;
            }
            x += item.advance;
            ++index;
        }
        return index;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), m_bar->palette().color(QPalette::Window));
        for (const Item &item : std::as_const(m_items)) {
            if (!item.hidden) {
                painter.drawPixmap(QPointF(item.x, item.top), item.image);
            }
        }
    }

private:
    static constexpr int FrameMs = 16;
    // Fraction of the remaining distance covered per frame, so a slide settles in about 150 ms.
    static constexpr qreal Ease = 0.3;
    static constexpr int TrailingSpacing = 2;

    struct Item
    {
        QPixmap image;
        int top = 0;
        int advance = 0;
        qreal x = 0;
        int target = 0;
        bool dragged = false;
        bool hidden = false;
    };

    void step()
    {
        bool moving = false;
        for (Item &item : m_items) {
            const qreal remaining = item.target - item.x;
            if (std::abs(remaining) < 0.5) {
                item.x = item.target;
            } else {
                item.x += remaining * Ease;
                moving = true;
            }
        }
        if (!moving) {
            m_timer.stop();
        }
        update();
    }

    QTabBar *m_bar;
    QList<Item> m_items;
    int m_origin = 0;
    QTimer m_timer;
};

PanelTabs::TabDrag PanelTabs::s_drag;

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
    setFocusedSide(false);

    connect(this, &QTabWidget::currentChanged, this, [this](int) {
        FilePanel *panel = currentPanel();
        emit currentPanelChanged(panel);
        if (panel != nullptr) {
            panel->focusView();
            emit panelActivated(panel);
        }
    });
    tabBar()->installEventFilter(this);
}

bool PanelTabs::eventFilter(QObject *watched, QEvent *event)
{
    // While a tab drag runs, this side also filters the whole application for Escape and focus loss.
    if (tabDragActive() && s_drag.source == this && watched != tabBar()) {
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            cancelTabDrag();
            return true;
        }
        if (event->type() == QEvent::ApplicationDeactivate) {
            cancelTabDrag();
        }
        return QTabWidget::eventFilter(watched, event);
    }
    if (watched != tabBar()) {
        return QTabWidget::eventFilter(watched, event);
    }
    if (tabDragActive() && s_drag.source == this) {
        if (event->type() == QEvent::MouseMove) {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            updateTabDrag(mouse->globalPosition().toPoint(), mouse->modifiers());
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                finishTabDrag(mouse->globalPosition().toPoint(), mouse->modifiers());
            }
            return true;
        }
    }
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        const int index = mouse->button() == Qt::LeftButton ? tabBar()->tabAt(mouse->position().toPoint()) : -1;
        // Track the panel, not the index: the bar may reorder tabs before the drag leaves it.
        m_pressPanel = index >= 0 ? qobject_cast<FilePanel *>(widget(index)) : nullptr;
        m_pressPos = mouse->position().toPoint();
        // Snapshot now: once the bar's own move starts, tabs are drawn away from their slots.
        m_pressImages.clear();
        if (index >= 0) {
            for (int i = 0; i < count(); ++i) {
                m_pressImages.insert(widget(i), tabBar()->grab(tabBar()->tabRect(i)));
            }
        }
        m_pressOffsetX = index >= 0 ? m_pressPos.x() - tabBar()->tabRect(index).left() : 0;
        break;
    }
    case QEvent::MouseMove: {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (!m_pressPanel || !(mouse->buttons() & Qt::LeftButton)) {
            break;
        }
        const QPoint pos = mouse->position().toPoint();
        const QRect zone = tabBar()->rect().adjusted(-DragOutMargin, -DragOutMargin, DragOutMargin, DragOutMargin);
        // With Ctrl held the drag is a duplicate, so start it at once instead of reordering in the bar.
        const bool ctrlDrag = mouse->modifiers().testFlag(Qt::ControlModifier)
            && (pos - m_pressPos).manhattanLength() >= QApplication::startDragDistance();
        if (ctrlDrag || !zone.contains(pos)) {
            beginTabDrag(m_pressPanel, mouse->globalPosition().toPoint());
            return true;
        }
        if (mouse->modifiers().testFlag(Qt::ControlModifier)) {
            // Below the drag threshold: keep the bar from starting its own move.
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

bool PanelTabs::tabDragActive() const
{
    return s_drag.source != nullptr;
}

void PanelTabs::beginTabDrag(FilePanel *panel, const QPoint &globalPos)
{
    m_pressPanel = nullptr;
    // End the tab bar's own in-bar move first, or it stays stuck mid-drag.
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(m_pressPos), tabBar()->mapToGlobal(QPointF(m_pressPos)),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(tabBar(), &release);
    const int index = indexOf(panel);
    QWidget *top = window();
    if (index < 0 || top == nullptr || tabDragActive()) {
        return;
    }

    const QRect tabRect = tabBar()->tabRect(index);
    auto *image = new QLabel(top);
    const QPixmap pressed = m_pressImages.value(panel);
    image->setPixmap(pressed.isNull() ? tabBar()->grab(tabRect) : pressed);
    image->setAttribute(Qt::WA_TransparentForMouseEvents);
    image->adjustSize();
    auto *marker = new QFrame(top);
    marker->setAttribute(Qt::WA_TransparentForMouseEvents);
    marker->setStyleSheet(QStringLiteral("QFrame { background: palette(highlight); }"));
    marker->resize(DropMarkerWidth, tabRect.height());
    marker->hide();

    s_drag.source = this;
    s_drag.panel = panel;
    s_drag.image = image;
    s_drag.marker = marker;
    // Where the cursor held the tab at press. The tab may have slid in its bar since then.
    s_drag.grabOffsetX = m_pressOffsetX;
    s_drag.rowTop = tabBar()->mapTo(top, tabRect.topLeft()).y();
    (void)ghostFor(this); // The source row redraws from the start, so the dragged tab can leave it.

    image->raise();
    image->show();
    // The tab bar receives every move and the release, even outside it.
    tabBar()->grabMouse();
    qApp->installEventFilter(this);
    QApplication::setOverrideCursor(Qt::ArrowCursor);
    updateTabDrag(globalPos, QGuiApplication::keyboardModifiers());
}

TabGhost *PanelTabs::ghostFor(PanelTabs *tabs)
{
    QPointer<TabGhost> &ghost = s_drag.ghosts[tabs];
    if (ghost == nullptr && tabs->tabBar()->isVisible() && window() != nullptr) {
        QList<QWidget *> pages;
        for (int i = 0; i < tabs->count(); ++i) {
            pages.append(tabs->widget(i));
        }
        const bool source = tabs == s_drag.source;
        ghost = new TabGhost(tabs->tabBar(), source ? s_drag.panel.data() : nullptr,
                             source ? m_pressImages : QHash<QWidget *, QPixmap>(), pages, window());
        if (s_drag.image != nullptr) {
            s_drag.image->raise();
        }
    }
    return ghost;
}

PanelTabs *PanelTabs::dropTargetAt(int globalX) const
{
    QWidget *top = window();
    if (top == nullptr) {
        return nullptr;
    }
    // Probe the middle of the tab row, so the vertical cursor position never matters.
    const QPoint probe(globalX, top->mapToGlobal(QPoint(0, s_drag.rowTop + tabBar()->height() / 2)).y());
    for (QWidget *w = QApplication::widgetAt(probe); w != nullptr; w = w->parentWidget()) {
        if (auto *tabs = qobject_cast<PanelTabs *>(w)) {
            return tabs;
        }
    }
    return nullptr;
}

int PanelTabs::dropIndex(PanelTabs *target, int globalX, bool copy)
{
    const bool showDragged = copy || target != s_drag.source;
    TabGhost *ghost = ghostFor(target);
    if (ghost == nullptr) {
        // A hidden bar has one tab. Without Ctrl on its own side there is nowhere else to go.
        return showDragged ? target->count() : 0;
    }
    return ghost->indexAt(target->tabBar()->mapFromGlobal(QPoint(globalX, 0)).x(), showDragged);
}

void PanelTabs::updateTabDrag(const QPoint &globalPos, Qt::KeyboardModifiers modifiers)
{
    QWidget *top = window();
    if (top == nullptr || s_drag.image == nullptr || s_drag.marker == nullptr) {
        cancelTabDrag();
        return;
    }
    const int x = top->mapFromGlobal(globalPos).x() - s_drag.grabOffsetX;
    const int maxX = std::max(0, top->width() - s_drag.image->width());
    s_drag.image->move(std::clamp(x, 0, maxX), s_drag.rowTop);

    const bool copy = modifiers.testFlag(Qt::ControlModifier);
    PanelTabs *target = dropTargetAt(globalPos.x());
    const int index = target != nullptr ? dropIndex(target, globalPos.x(), copy) : -1;
    const int gapWidth = s_drag.image->width() + 2;
    for (auto it = s_drag.ghosts.begin(); it != s_drag.ghosts.end(); ++it) {
        if (it.value() == nullptr) {
            continue;
        }
        const bool showDragged = copy || it.key() != s_drag.source;
        it.value()->setLayout(showDragged, it.key() == target ? index : -1, gapWidth);
    }
    if (target == nullptr) {
        s_drag.marker->hide();
        QApplication::changeOverrideCursor(Qt::ForbiddenCursor);
        return;
    }
    // A move keeps the plain pointer, as in the tab bar's own drag. Only a copy is marked.
    QApplication::changeOverrideCursor(copy ? Qt::DragCopyCursor : Qt::ArrowCursor);
    if (s_drag.ghosts.value(target) != nullptr) {
        s_drag.marker->hide();
    } else {
        // The bar is hidden, so mark the start of the side instead of opening a gap.
        s_drag.marker->move(target->mapTo(top, QPoint(0, 0)).x(), s_drag.rowTop);
        s_drag.marker->raise();
        s_drag.marker->show();
    }
    s_drag.image->raise();
}

void PanelTabs::finishTabDrag(const QPoint &globalPos, Qt::KeyboardModifiers modifiers)
{
    const bool copy = modifiers.testFlag(Qt::ControlModifier);
    PanelTabs *target = dropTargetAt(globalPos.x());
    const int index = target != nullptr ? dropIndex(target, globalPos.x(), copy) : -1;
    FilePanel *panel = s_drag.panel;
    PanelTabs *source = s_drag.source;
    cancelTabDrag();
    if (target == nullptr || panel == nullptr || source == nullptr || source->indexOf(panel) < 0) {
        return;
    }
    if (copy) {
        target->duplicateTab(panel, index);
        return;
    }
    if (target != source) {
        source->moveTabTo(panel, target, index);
        return;
    }
    // Same side: index already counts the tabs without the dragged one.
    const int from = source->indexOf(panel);
    const int to = std::clamp(index, 0, source->count() - 1);
    if (to != from) {
        source->tabBar()->moveTab(from, to);
    }
    source->setCurrentWidget(panel);
    source->activateAfterDrop(panel);
}

void PanelTabs::cancelTabDrag()
{
    if (!tabDragActive()) {
        return;
    }
    PanelTabs *source = s_drag.source;
    delete s_drag.image;
    delete s_drag.marker;
    for (const QPointer<TabGhost> &ghost : std::as_const(s_drag.ghosts)) {
        delete ghost.data();
    }
    s_drag = TabDrag();
    if (source != nullptr) {
        source->tabBar()->releaseMouse();
        source->m_pressImages.clear();
        qApp->removeEventFilter(source);
    }
    QApplication::restoreOverrideCursor();
}

void PanelTabs::duplicateTab(const FilePanel *original, int index)
{
    FilePanel *panel = addPanel(index, {original->persistentPath(), original->sortColumn(), original->sortOrder()});
    if (original->isShowingResults() && original->searchResults()) {
        panel->showSearchResults(original->searchResults());
    }
    setCurrentWidget(panel);
    activateAfterDrop(panel);
}

void PanelTabs::activateAfterDrop(FilePanel *panel)
{
    // Windows restores focus to the drag source when the drag ends, so re-assert it afterwards.
    QTimer::singleShot(0, panel, [this, panel] {
        panel->focusView();
        emit panelActivated(panel);
    });
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
    target->activateAfterDrop(panel);
}

void PanelTabs::adoptPanel(FilePanel *panel, int index)
{
    panel->setShowHidden(m_showHidden);
    panel->setShowFolderSizes(m_showFolderSizes);
    connect(panel, &FilePanel::activated, this, &PanelTabs::panelActivated);
    connect(panel, &FilePanel::switchPanelRequested, this, &PanelTabs::switchPanelRequested);
    connect(panel, &FilePanel::pathChanged, this, [this, panel] { updateTitle(panel); });
    connect(panel, &FilePanel::currentItemChanged, this, [this, panel] { emit currentItemChanged(panel); });
    connect(panel, &FilePanel::contextMenuRequested, this, &PanelTabs::contextMenuRequested);
    connect(panel, &FilePanel::filesDropped, this, &PanelTabs::filesDropped);
    connect(panel, &FilePanel::renameRequested, this, &PanelTabs::renameRequested);
    insertTab(std::clamp(index, 0, count()), panel, QString());
    updateTitle(panel);
}

FilePanel *PanelTabs::currentPanel() const
{
    return qobject_cast<FilePanel *>(currentWidget());
}

void PanelTabs::setFocusedSide(bool focused)
{
    // The focused side is already marked by its blue address bar, so its tab drops the underline.
    // The border stays 3px either way so the tab height does not jump.
    m_focusedSide = focused;
    // Mixed from window and text, because the dark palette's window, base and mid roles are too close to tell apart.
    const QPalette pal = palette();
    const QColor window = pal.color(QPalette::Window);
    const QColor text = pal.color(QPalette::WindowText);
    const auto mix = [&window, &text](qreal amount) {
        return QColor::fromRgbF(window.redF() + (text.redF() - window.redF()) * amount,
                                window.greenF() + (text.greenF() - window.greenF()) * amount,
                                window.blueF() + (text.blueF() - window.blueF()) * amount)
            .name();
    };
    constexpr int DarkLightness = 128;
    const bool dark = window.lightness() < DarkLightness;
    const QString selected = dark ? mix(0.18) : pal.color(QPalette::Base).name();
    // Filled with the tab colour, not transparent, so the darker bar does not show through as a line.
    const QString underline = focused ? selected : QStringLiteral("palette(highlight)");
    tabBar()->setStyleSheet(QStringLiteral(
        "QTabBar::tab { padding: 4px 12px; margin-right: 2px; border: 1px solid %2;"
        "  border-bottom: 3px solid transparent; border-top-left-radius: 3px; border-top-right-radius: 3px;"
        "  background: %3; color: %4; }"
        "QTabBar::tab:hover:!selected { color: %5; }"
        "QTabBar::tab:selected { font-weight: bold; background: %6; color: %5;"
        "  border-bottom: 3px solid %1; }")
        .arg(underline, mix(dark ? 0.32 : 0.25), window.name(), mix(0.55), text.name(), selected));
}

void PanelTabs::refreshColours()
{
    // An empty palette re-resolves from the parent. Qt does not propagate a theme switch past this widget.
    setPalette(QPalette());
    setFocusedSide(m_focusedSide);
    for (FilePanel *panel : panels()) {
        panel->refreshColours();
    }
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

void PanelTabs::setShowFolderSizes(bool show)
{
    m_showFolderSizes = show;
    for (FilePanel *panel : panels()) {
        panel->setShowFolderSizes(show);
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
    panel->setShowFolderSizes(m_showFolderSizes);
    adoptPanel(panel, index);
    panel->restorePath(state.path);
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

} // namespace bifrons
