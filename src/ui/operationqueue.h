#pragma once

#include <QList>
#include <QPointer>
#include <QWidget>

#include "core/fileoperation.h"

class QLabel;
class QProgressBar;
class QToolButton;

namespace bifrons {

class OperationRunner;

// One side's file operations: they run one at a time in the background, in the order queued, with
// progress in this strip. The strip is hidden while the queue is idle.
class OperationQueue : public QWidget
{
    Q_OBJECT

public:
    explicit OperationQueue(QWidget *window, QWidget *parent = nullptr);
    ~OperationQueue() override;

    // Takes ownership of op.
    void enqueue(FileOperation *op, const QString &title);
    bool isBusy() const;
    // Drops queued operations and cancels the running one. finished() still follows for it.
    void cancelAll();

signals:
    void finished(const bifrons::FileOperation::Result &result);

private:
    struct Pending
    {
        FileOperation *op = nullptr;
        QString title;
    };

    void startNext();
    void onRunnerFinished(const FileOperation::Result &result);
    void showProgress(int permille, const QString &current);

    QWidget *m_window;
    QPointer<OperationRunner> m_runner;
    QList<Pending> m_pending;
    QLabel *m_label = nullptr;
    QProgressBar *m_bar = nullptr;
    QToolButton *m_cancel = nullptr;
};

} // namespace bifrons
