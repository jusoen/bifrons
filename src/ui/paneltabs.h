#pragma once

#include <QHash>
#include <QPixmap>
#include <QPointer>
#include <QTabWidget>

#include <memory>

#include "core/filesort.h"
#include "core/filesource.h"

class QFrame;
class QLabel;

namespace bifrons {

class FilePanel;
class TabGhost;

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
    // Restyles the selected tab for the side that has focus.
    void setFocusedSide(bool focused);
    // Re-applies colours after a theme switch, here and in every tab.
    void refreshColours();

    // Opens a tab after the current one showing the same folder, and switches to it.
    void newTab();
    // Closes the current tab unless it is the last one.
    void closeCurrentTab();
    void nextTab();
    void previousTab();

    // Applies to every tab, including ones opened later.
    void setShowHidden(bool show);
    void setShowFolderSizes(bool show);

    QList<TabState> state() const;
    // Replaces all tabs. An empty list leaves a single tab at the home folder.
    void restore(const QList<TabState> &tabs, int currentIndex);

signals:
    void panelActivated(bifrons::FilePanel *panel);
    void switchPanelRequested();
    void currentItemChanged(bifrons::FilePanel *panel);
    void contextMenuRequested(bifrons::FilePanel *panel, QWidget *anchor, const QPoint &localPos, bool onItem);
    void filesDropped(bifrons::FilePanel *panel, const QStringList &sources, const QString &targetDir, bool move);
    void renameRequested(bifrons::FilePanel *panel, const QString &path, const QString &newName);
    void currentPanelChanged(bifrons::FilePanel *panel);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    FilePanel *addPanel(int index, const TabState &state);
    void adoptPanel(FilePanel *panel, int index);
    void moveTabTo(FilePanel *panel, PanelTabs *target, int index);
    void duplicateTab(const FilePanel *original, int index);
    void activateAfterDrop(FilePanel *panel);

    // Tab drag that keeps the tab image in the tab row, across both sides.
    // Ctrl at release duplicates the tab, otherwise it moves.
    struct TabDrag
    {
        QPointer<PanelTabs> source;
        QPointer<FilePanel> panel;
        QPointer<QLabel> image;
        QPointer<QFrame> marker;
        // Cursor offset into the tab image, so the image does not jump under the cursor.
        int grabOffsetX = 0;
        // Tab row top in window coordinates. The image never leaves it.
        int rowTop = 0;
        // Overlays that redraw each side's tab row with a gap sliding to the drop point.
        QHash<PanelTabs *, QPointer<TabGhost>> ghosts;
    };
    void beginTabDrag(FilePanel *panel, const QPoint &globalPos);
    void updateTabDrag(const QPoint &globalPos, Qt::KeyboardModifiers modifiers);
    void finishTabDrag(const QPoint &globalPos, Qt::KeyboardModifiers modifiers);
    void cancelTabDrag();
    // Side under the tab row at globalX, or nullptr when none.
    PanelTabs *dropTargetAt(int globalX) const;
    // Where a tab dropped on target at globalX goes, as an index into target's tabs after the
    // dragged tab is taken out (a move within its own side) or left in (a copy, or another side).
    int dropIndex(PanelTabs *target, int globalX, bool copy);
    // The overlay for tabs, created on first use. nullptr while its tab bar is hidden.
    TabGhost *ghostFor(PanelTabs *tabs);
    bool tabDragActive() const;

    static TabDrag s_drag;

    bool m_showHidden = false;
    bool m_showFolderSizes = false;
    bool m_focusedSide = false;
    QPointer<FilePanel> m_pressPanel;
    QPoint m_pressPos;
    // Every tab's image at press, before the bar's own move starts sliding them.
    QHash<QWidget *, QPixmap> m_pressImages;
    int m_pressOffsetX = 0;
    void closeTab(int index);
    void updateTitle(FilePanel *panel);

    std::shared_ptr<const FileSource> m_source;
};

} // namespace bifrons
