#pragma once

#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QTimer>
#include <QWidget>

#include <memory>

#include "core/filesort.h"
#include "core/filesource.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QTableView;

namespace dcpp {

class FileListModel;
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

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void loadFrom(std::shared_ptr<const FileSource> source, const QString &path, const QString &selectName);
    void onLoadFinished();
    void openCurrent();
    void goParent();
    void markAndMoveDown();
    void selectName(const QString &name);
    void updateStatus();
    void updateDriveCombo();
    void showError(const QString &message);

    std::shared_ptr<const FileSource> m_local;
    std::shared_ptr<const FileSource> m_source;
    std::shared_ptr<const FileSource> m_loadingSource;
    std::shared_ptr<const SearchResultFileSource> m_results;
    FileListModel *m_model = nullptr;
    QTableView *m_view = nullptr;
    QLineEdit *m_pathEdit = nullptr;
    QComboBox *m_drives = nullptr;
    QLabel *m_status = nullptr;

    QFutureWatcher<ListResult> m_loader;
    QFileSystemWatcher m_fsWatcher;
    QTimer m_refreshTimer;

    QString m_path;
    QString m_loadingPath;
    QString m_selectAfterLoad;
    QString m_cursorBeforeReset;
    int m_redirects = 0;
    bool m_keepCursor = true;
    bool m_active = false;
};

} // namespace dcpp
