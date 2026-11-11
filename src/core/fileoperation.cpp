#include "core/fileoperation.h"

#include <QDir>

namespace bifrons {

namespace {
constexpr qint64 ProgressIntervalMs = 50;
}

FileOperation::FileOperation(QObject *parent)
    : QObject(parent)
{
}

void FileOperation::setQueryHandler(QueryHandler handler)
{
    m_handler = std::move(handler);
}

void FileOperation::cancel()
{
    m_cancelled.store(true);
}

bool FileOperation::isCancelled() const
{
    return m_cancelled.load();
}

FileOperation::Result FileOperation::result() const
{
    return m_result;
}

void FileOperation::run()
{
    m_result = Result{};
    m_progressTimer.start();
    execute();
    if (isCancelled()) {
        m_result.aborted = true;
    }
    emit finished();
}

FileOperation::Answer FileOperation::ask(const Query &query)
{
    if (isCancelled() || !m_handler) {
        cancel();
        return Answer::Abort;
    }
    const Answer answer = m_handler(query);
    if (answer == Answer::Abort) {
        cancel();
    }
    return answer;
}

void FileOperation::reportProgress(qint64 done, qint64 total, const QString &current, bool force)
{
    const bool changed = current != m_lastReported;
    if (!force && !changed && m_progressTimer.elapsed() < ProgressIntervalMs) {
        return;
    }
    m_lastReported = current;
    m_progressTimer.restart();
    emit progress(done, total, current);
}

void FileOperation::recordSkip(const QString &path, const QString &message)
{
    ++m_result.skipped;
    m_result.errors.append(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(path), message));
}

} // namespace bifrons
