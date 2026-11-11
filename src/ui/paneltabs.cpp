#include "ui/paneltabs.h"

#include <QDir>
#include <QMouseEvent>
#include <QTabBar>

#include <algorithm>

#include "core/localfilesource.h"
#include "ui/filepanel.h"

namespace dcpp {

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
        "QTabBar::tab { padding: 4px 12px; margin-right: 1px; border: none;"
        "  border-bottom: 3px solid transparent; background: palette(window); color: palette(mid); }"
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
}

bool PanelTabs::eventFilter(QObject *watched, QEvent *event)
{
    // Middle-click closes a tab, as in DC and browsers.
    if (watched == tabBar() && event->type() == QEvent::MouseButtonRelease) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::MiddleButton) {
            closeTab(tabBar()->tabAt(mouse->position().toPoint()));
            return true;
        }
    }
    return QTabWidget::eventFilter(watched, event);
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

QList<TabState> PanelTabs::state() const
{
    QList<TabState> tabs;
    for (const FilePanel *panel : panels()) {
        tabs.append({panel->path(), panel->sortColumn(), panel->sortOrder()});
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
    connect(panel, &FilePanel::activated, this, &PanelTabs::panelActivated);
    connect(panel, &FilePanel::switchPanelRequested, this, &PanelTabs::switchPanelRequested);
    connect(panel, &FilePanel::pathChanged, this, [this, panel] { updateTitle(panel); });
    insertTab(index, panel, QString());
    panel->setPath(LocalFileSource::nearestExistingDir(state.path));
    return panel;
}

void PanelTabs::updateTitle(FilePanel *panel)
{
    const int index = indexOf(panel);
    if (index < 0) {
        return;
    }
    const QString name = QDir(panel->path()).dirName();
    // A drive root has no folder name, so show the root itself ("C:").
    const QString title = name.isEmpty() ? QDir::toNativeSeparators(panel->path()).remove(QLatin1Char('\\')) : name;
    setTabText(index, title);
    setTabToolTip(index, QDir::toNativeSeparators(panel->path()));
}

} // namespace dcpp
