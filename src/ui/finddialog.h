#pragma once

#include <QDialog>
#include <QThread>

#include "core/searchengine.h"

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace dcpp {

// Find Files (Alt+F7). Non-modal and reused, so the last criteria and results stay.
class FindDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FindDialog(QWidget *parent = nullptr);
    ~FindDialog() override;

    void setStartDir(const QString &dir);
    bool isSearching() const;
    // Thread-safe request to stop. searchFinished() follows.
    void stopSearch();

signals:
    void goToFile(const QString &path);
    void feedToPanel(const QString &baseDir, const QStringList &paths);
    void searchFinished();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void startSearch();
    void onFound(const QStringList &paths);
    void onProgress(const QString &dir, int scanned);
    void onEngineFinished();
    void browse();
    void updateButtons();
    QString selectedPath() const;
    SearchCriteria criteria() const;

    QLineEdit *m_startDir = nullptr;
    QLineEdit *m_mask = nullptr;
    QLineEdit *m_text = nullptr;
    QCheckBox *m_caseSensitive = nullptr;
    QComboBox *m_encoding = nullptr;
    QCheckBox *m_useMinSize = nullptr;
    QSpinBox *m_minSize = nullptr;
    QCheckBox *m_useMaxSize = nullptr;
    QSpinBox *m_maxSize = nullptr;
    QCheckBox *m_useAfter = nullptr;
    QDateEdit *m_after = nullptr;
    QCheckBox *m_useBefore = nullptr;
    QDateEdit *m_before = nullptr;
    QListWidget *m_results = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_start = nullptr;
    QPushButton *m_stop = nullptr;
    QPushButton *m_goTo = nullptr;
    QPushButton *m_feed = nullptr;

    QThread m_thread;
    SearchEngine *m_engine = nullptr;
    QString m_searchedDir;
    QStringList m_found;
};

} // namespace dcpp
