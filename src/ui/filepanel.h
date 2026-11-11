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

// Path bar, file list and status line. Listing runs on the thread pool.
class FilePanel : public QWidget
{
    Q_OBJECT

public:
    explicit FilePanel(std::shared_ptr<const FileSource> source, QWidget *parent = nullptr);

    QString path() const;
    // Lists path in the background, then selects selectName if given.
    void setPath(const QString &path, const QString &selectName = QString());
    void reload();

    // Marked entries, or the entry under the cursor when none are marked.
    QStringList selectedPaths() const;
    // Name under the cursor, empty on the ".." row.
    QString currentName() const;

    void setActive(bool active);
    void focusView();

    SortColumn sortColumn() const;
    Qt::SortOrder sortOrder() const;
    void setSort(SortColumn column, Qt::SortOrder order);

signals:
    void activated(dcpp::FilePanel *panel);
    void switchPanelRequested();
    void pathChanged(const QString &path);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void onLoadFinished();
    void openCurrent();
    void goParent();
    void markAndMoveDown();
    void markByMaskPrompt(bool mark);
    void selectName(const QString &name);
    void updateStatus();
    void updateDriveCombo();
    void showError(const QString &message);

    std::shared_ptr<const FileSource> m_source;
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
