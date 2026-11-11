#pragma once

#include <QMutex>
#include <QObject>
#include <QThread>
#include <QWaitCondition>

#include <optional>

#include "core/fileoperation.h"

class QProgressDialog;
class QWidget;

namespace bifrons {

// Runs a FileOperation on its own thread. Delete only after finished().
class OperationRunner : public QObject
{
    Q_OBJECT

public:
    // Takes ownership of op.
    OperationRunner(FileOperation *op, const QString &title, QWidget *window);
    ~OperationRunner() override;

    void start();
    void cancel();
    bool isRunning() const;

signals:
    void finished(const bifrons::FileOperation::Result &result);

private:
    FileOperation::Answer askFromWorker(const FileOperation::Query &query);
    void showQuery(quint64 id, const FileOperation::Query &query);
    void onProgress(qint64 done, qint64 total, const QString &current);
    void onOperationFinished();

    FileOperation *m_op;
    QWidget *m_window;
    QProgressDialog *m_dialog = nullptr;
    QThread m_thread;
    bool m_running = false;

    // Guards the query hand-off between the worker and the UI thread.
    QMutex m_mutex;
    QWaitCondition m_answered;
    quint64 m_queryId = 0;
    std::optional<FileOperation::Answer> m_answer;
};

} // namespace bifrons
