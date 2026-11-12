#pragma once

#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QTimer>
#include <QWidget>

#include <atomic>
#include <memory>

#include "core/filesort.h"
#include "core/filesource.h"

class QLabel;
class QLineEdit;
class QTableView;

namespace bifrons {

class FileListModel;
class PathBar;
class SearchResultFileSource;

// Path bar, file list and status line. Listing runs on the thread pool.
class FilePanel : public QWidget
{
    Q_OBJECT

public:
    explicit FilePanel(std::shared_ptr<const FileSource> source, QWidget *parent = nullptr);
    ~FilePanel() override;

    QString path() const;
    // Lists a local path in the background, then selects selectName if given.
    void setPath(const QString &path, const QString &selectName = QString());
    void reload();
    // Opens a saved path. Network paths load in the background and are kept even if unreachable.
    void restorePath(const QString &path);

    void showSearchResults(std::shared_ptr<const SearchResultFileSource> results);
    bool isShowingResults() const;
    std::shared_ptr<const SearchResultFileSource> searchResults() const;
    // A real folder to save and restore: the searched folder when showing results.
    QString persistentPath() const;
    // Full path of the entry under the cursor, empty on the ".." row.
    QString currentPath() const;
    // Files (not folders) in display order, for next/previous in the viewer.
    QStringList filePaths() const;

    // Marked entries, or the entry under the cursor when none are marked.
    QStringList selectedPaths() const;
    // Name under the cursor, empty on the ".." row.
    QString currentName() const;

    void setActive(bool active);
    // Opens the filter box under the list and focuses it. Typing narrows the list as you go.
    void showQuickFilter();
    // Edits the name under the cursor in the list. False where that is not possible (search results),
    // so the caller can fall back to a dialog.
    bool startInlineRename();
    // Why sources cannot be pasted into this panel's folder, or empty when they can.
    QString pasteProblem(const QStringList &sources) const;
    // Stops watching this panel's folder when it is one of paths or inside one. Windows cannot
    // remove or rename a folder while a change watch holds it open.
    void releaseFolderUnder(const QStringList &paths);
    // Reloads after releaseFolderUnder let go, so a vanished folder falls back to the nearest one left.
    void reloadIfReleased();
    // Re-applies colours after a theme switch.
    void refreshColours();
    void focusView();

    // Folder history of this panel, newest last.
    bool canGoBack() const;
    bool canGoForward() const;
    bool canGoUp() const;
    // False while a network folder is loading or unreachable, where starting a terminal would block.
    bool canOpenTerminal() const;
    void goBack();
    void goForward();
    void goParent();
    void openTerminalHere();

    void setShowHidden(bool show);
    // Counts each subfolder's total size in the background and shows it in the Size column.
    void setShowFolderSizes(bool show);
    void markByMaskPrompt(bool mark);
    void markAll(bool mark);

    SortColumn sortColumn() const;
    Qt::SortOrder sortOrder() const;
    void setSort(SortColumn column, Qt::SortOrder order);

signals:
    void activated(bifrons::FilePanel *panel);
    void switchPanelRequested();
    void pathChanged(const QString &path);
    void currentItemChanged();
    // Path, history or source changed after a load.
    void navigationChanged();
    // Right-click or the menu key. localPos is in anchor. onItem is false for empty space and "..".
    void contextMenuRequested(bifrons::FilePanel *panel, QWidget *anchor, const QPoint &localPos, bool onItem);
    // Local files dropped on this panel, to copy or move into targetDir. Sent after the drop has finished.
    void filesDropped(bifrons::FilePanel *panel, const QStringList &sources, const QString &targetDir, bool move);
    // The user renamed the entry at path in place. newName is a bare name, already checked.
    void renameRequested(bifrons::FilePanel *panel, const QString &path, const QString &newName);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    // Only the tab on show counts folder sizes, so hidden tabs never hold up the visible ones.
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void loadFrom(std::shared_ptr<const FileSource> source, const QString &path, const QString &selectName);
    void onLoadFinished();
    void openCurrent();
    void markAndMoveDown();
    void selectName(const QString &name);
    void updateStatus();
    // Clears and hides the filter box. focusList returns the keyboard to the list.
    void closeQuickFilter(bool focusList);
    // Enter in the rename box. Keeps the box open when the name is not usable.
    void commitInlineRename();
    void cancelInlineRename();
    // Drags the marked entries if row is marked, otherwise just row.
    void startDrag(int row);
    // Folder a drop at pos would go into, or empty where dropping makes no sense. row gets the tinted row.
    QString dropTarget(const QPoint &pos, const QStringList &sources, int *row) const;
    // Handles drag enter, move, leave and drop on the list. Returns true when the event was used.
    bool handleDropEvent(QEvent *event);
    void showError(const QString &message);
    // Centred text over the file list. Empty hides it.
    void showMessage(const QString &text);
    void enterLocation(const QString &path, const FileItemList &items, bool reachable);
    void updatePathBar();
    // Cancels any count in progress, then counts the current folder's subfolders if enabled.
    void startFolderSizes();
    void stopFolderSizes();
    void recordHistory(const QString &path);
    void goToHistory(int index);

    std::shared_ptr<const FileSource> m_local;
    std::shared_ptr<const FileSource> m_source;
    std::shared_ptr<const FileSource> m_loadingSource;
    std::shared_ptr<const SearchResultFileSource> m_results;
    FileListModel *m_model = nullptr;
    QTableView *m_view = nullptr;
    PathBar *m_pathBar = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_message = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    bool m_folderReleased = false;
    // Open only while renaming in place.
    QLineEdit *m_renameEdit = nullptr;
    QString m_renameFrom;
    // Selected by the next reload, so the cursor follows a renamed entry.
    QString m_renamedTo;
    // Where a plain left press landed, for starting a drag. -1 when no drag can start.
    int m_pressRow = -1;
    QPoint m_pressPos;

    QFutureWatcher<ListResult> m_loader;
    QFileSystemWatcher m_fsWatcher;
    QTimer m_refreshTimer;
    // Delays the connecting message so fast network loads do not flash it.
    QTimer m_connectingTimer;

    QString m_path;
    QString m_loadingPath;
    QString m_selectAfterLoad;
    QString m_cursorBeforeReset;
    QStringList m_history;
    int m_historyIndex = -1;
    // History entry being loaded by back/forward, -1 for any other load.
    int m_historyTarget = -1;
    int m_redirects = 0;
    bool m_networkPending = false;
    bool m_showFolderSizes = false;
    // Set to stop the running folder size count. Replaced for each new count.
    std::shared_ptr<std::atomic<bool>> m_sizeCancel;
    // Results from an older count are dropped.
    quint64 m_sizeGeneration = 0;
    bool m_unreachable = false;
    bool m_keepCursor = true;
    bool m_active = false;
};

} // namespace bifrons
