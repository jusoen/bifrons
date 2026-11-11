#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>

#include <atomic>
#include <functional>

namespace bifrons {

// Base for long-running file work. run() executes on the calling thread.
class FileOperation : public QObject
{
    Q_OBJECT

public:
    enum class Answer
    {
        Overwrite,
        OverwriteAll,
        Skip,
        SkipAll,
        Rename,
        Retry,
        Abort
    };

    struct Query
    {
        enum class Kind
        {
            Collision,
            Error
        };
        Kind kind = Kind::Error;
        QString source;
        QString target;
        QString message;
    };

    struct Result
    {
        int processed = 0;
        int skipped = 0;
        bool aborted = false;
        QStringList errors;
    };

    // Called on the worker thread. Must return an answer valid for the query kind.
    using QueryHandler = std::function<Answer(const Query &)>;

    explicit FileOperation(QObject *parent = nullptr);

    void setQueryHandler(QueryHandler handler);
    // Thread-safe. The operation stops at the next check point.
    void cancel();
    bool isCancelled() const;
    // Valid once finished() has been emitted.
    Result result() const;

public slots:
    void run();

signals:
    // done and total are in bytes for transfers and in items for deletes.
    void progress(qint64 done, qint64 total, const QString &current);
    void finished();

protected:
    virtual void execute() = 0;

    // Returns Abort when no handler is set or the operation is cancelled.
    Answer ask(const Query &query);
    void reportProgress(qint64 done, qint64 total, const QString &current, bool force = false);
    void recordSkip(const QString &path, const QString &message);

    Result m_result;

private:
    QueryHandler m_handler;
    std::atomic<bool> m_cancelled{false};
    QElapsedTimer m_progressTimer;
    QString m_lastReported;
};

} // namespace bifrons
