#pragma once

#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QTimer>
#include <QWidget>

#include <memory>

#include "core/filesort.h"
#include "core/filesource.h"

class QLabel;
class QTableView;

namespace dcpp {

class FileListModel;
class PathBar;
class SearchResultFileSource;

// Path bar, file list and status line. Listing runs on the thread pool.
class FilePanel : public QWidget
{
    Q_OBJECT

public:
    explicit FilePanel(std::shared_ptr<const FileSource> source, QWidget *parent = nullptr);

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
    void markByMaskPrompt(bool mark);
    void markAll(bool mark);

    SortColumn sortColumn() const;
    Qt::SortOrder sortOrder() const;
    void setSort(SortColumn column, Qt::SortOrder order);

signals:
    void activated(dcpp::FilePanel *panel);
    void switchPanelRequested();
    void pathChanged(const QString &path);
    void currentItemChanged();
    // Path, history or source changed after a load.
    void navigationChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void loadFrom(std::shared_ptr<const FileSource> source, const QString &path, const QString &selectName);
    void onLoadFinished();
    void openCurrent();
    void markAndMoveDown();
    void selectName(const QString &name);
    void updateStatus();
    void showError(const QString &message);
    // Centred text over the file list. Empty hides it.
    void showMessage(const QString &text);
    void enterLocation(const QString &path, const FileItemList &items, bool reachable);
    void updatePathBar();
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
    bool m_unreachable = false;
    bool m_keepCursor = true;
    bool m_active = false;
};

} // namespace dcpp
