#pragma once

#include <QPointer>
#include <QTabWidget>

#include <memory>

#include "core/filesort.h"
#include "core/filesource.h"

class QMimeData;

namespace dcpp {

class FilePanel;

// Saved state of one tab.
struct TabState
{
    QString path;
    SortColumn sortColumn = SortColumn::Name;
    Qt::SortOrder sortOrder = Qt::AscendingOrder;
};

// One side of the main window: a FilePanel per tab. Always holds at least one tab.
class PanelTabs : public QTabWidget
{
    Q_OBJECT

public:
    explicit PanelTabs(std::shared_ptr<const FileSource> source, QWidget *parent = nullptr);

    FilePanel *currentPanel() const;
    QList<FilePanel *> panels() const;

    // Opens a tab after the current one showing the same folder, and switches to it.
    void newTab();
    // Closes the current tab unless it is the last one.
    void closeCurrentTab();
    void nextTab();
    void previousTab();

    // Applies to every tab, including ones opened later.
    void setShowHidden(bool show);

    QList<TabState> state() const;
    // Replaces all tabs. An empty list leaves a single tab at the home folder.
    void restore(const QList<TabState> &tabs, int currentIndex);

signals:
    void panelActivated(dcpp::FilePanel *panel);
    void switchPanelRequested();
    void currentItemChanged(dcpp::FilePanel *panel);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    FilePanel *addPanel(int index, const TabState &state);
    void adoptPanel(FilePanel *panel, int index);
    void moveTabTo(FilePanel *panel, PanelTabs *target, int index);
    void startTabDrag(FilePanel *panel);
    bool acceptsTabDrop(const QMimeData *mime) const;

    // The tab being dragged between sides. Drags never cross processes.
    static QPointer<PanelTabs> s_dragSource;
    static QPointer<FilePanel> s_dragPanel;

    bool m_showHidden = false;
    QPointer<FilePanel> m_pressPanel;
    QPoint m_pressPos;
    void closeTab(int index);
    void updateTitle(FilePanel *panel);

    std::shared_ptr<const FileSource> m_source;
};

} // namespace dcpp
