#include "ui/demooperation.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QThread>

namespace bifrons {

namespace {

constexpr int FileCount = 12;
constexpr qint64 ChunkBytes = 1024 * 1024;
constexpr int ChunksPerFile = 16; // 16 MiB per file, 192 MiB in all.
// Pause per copied chunk: 192 chunks x 60 ms keeps the copy on screen for about 12 s.
constexpr unsigned long ThrottleMs = 60;

} // namespace

void DemoOperation::execute()
{
    const QString root = QDir(QDir::tempPath())
                             .filePath(QStringLiteral("bifrons-demo-%1-%2")
                                           .arg(QCoreApplication::applicationPid())
                                           .arg(QDateTime::currentMSecsSinceEpoch()));
    const QString sourceDir = root + QStringLiteral("/source");
    const QString targetDir = root + QStringLiteral("/target");
    if (!QDir().mkpath(sourceDir) || !QDir().mkpath(targetDir)) {
        recordSkip(root, QStringLiteral("Cannot create the scratch folder"));
        (void)QDir(root).removeRecursively(); // Best effort: nothing else to clean if this fails.
        return;
    }

    const qint64 fileBytes = ChunkBytes * ChunksPerFile;
    // One bar for both phases: writing the source files, then copying them.
    const qint64 total = 2 * fileBytes * FileCount;
    qint64 done = 0;
    const QByteArray chunk(static_cast<int>(ChunkBytes), 'x');

    for (int i = 0; i < FileCount && !isCancelled() && m_result.errors.isEmpty(); ++i) {
        const QString name = QStringLiteral("demo-%1.bin").arg(i + 1, 2, 10, QLatin1Char('0'));
        QFile out(sourceDir + QLatin1Char('/') + name);
        if (!out.open(QIODevice::WriteOnly)) {
            recordSkip(out.fileName(), out.errorString());
            break;
        }
        for (int c = 0; c < ChunksPerFile && !isCancelled(); ++c) {
            if (out.write(chunk) != chunk.size()) {
                recordSkip(out.fileName(), out.errorString());
                break;
            }
            done += ChunkBytes;
            reportProgress(done, total, QStringLiteral("Preparing test files: %1").arg(name));
        }
    }

    for (int i = 0; i < FileCount && !isCancelled() && m_result.errors.isEmpty(); ++i) {
        const QString name = QStringLiteral("demo-%1.bin").arg(i + 1, 2, 10, QLatin1Char('0'));
        QFile in(sourceDir + QLatin1Char('/') + name);
        QFile out(targetDir + QLatin1Char('/') + name);
        if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)) {
            recordSkip(in.fileName(), in.isOpen() ? out.errorString() : in.errorString());
            break;
        }
        // Bounded by the file size: each pass reads up to one chunk and stops at end of file.
        while (!isCancelled()) {
            const QByteArray data = in.read(ChunkBytes);
            if (data.isEmpty()) {
                break;
            }
            if (out.write(data) != data.size()) {
                recordSkip(out.fileName(), out.errorString());
                break;
            }
            done += data.size();
            reportProgress(done, total, name);
            QThread::msleep(ThrottleMs);
        }
        ++m_result.processed;
    }

    reportProgress(total, total, QStringLiteral("Removing test files"), true);
    if (!QDir(root).removeRecursively()) {
        recordSkip(root, QStringLiteral("Could not remove the scratch folder"));
    }
}

} // namespace bifrons
