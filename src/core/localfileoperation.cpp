#include "core/localfileoperation.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <utility>

namespace bifrons {

namespace {

constexpr qint64 ChunkSize = 1024 * 1024;
constexpr int MaxUniqueNameTries = 9999;
const QLatin1String PartSuffix(".bifrons-part");
const QLatin1String BackupSuffix(".bifrons-bak");

Qt::CaseSensitivity pathCase()
{
#if defined(Q_OS_WIN)
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

bool samePath(const QString &a, const QString &b)
{
    return QDir::cleanPath(a).compare(QDir::cleanPath(b), pathCase()) == 0;
}

bool isInside(const QString &child, const QString &parent)
{
    const QString p = QDir::cleanPath(parent) + QLatin1Char('/');
    return QDir::cleanPath(child).startsWith(p, pathCase());
}

bool pathExists(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}

// Clears read-only so the file can be renamed over or removed on Windows.
void makeWritable(const QString &path)
{
    const QFile::Permissions p = QFile::permissions(path);
    // Best effort: if it fails, the following remove reports the real error.
    (void)QFile::setPermissions(path, p | QFile::WriteOwner | QFile::WriteUser);
}

bool removeFile(const QString &path)
{
    makeWritable(path);
    return QFile::remove(path);
}

} // namespace

QString uniqueTargetName(const QString &target)
{
    const QFileInfo info(target);
    const QString dir = info.absolutePath();
    const QString fileName = info.fileName();
    const qsizetype dot = info.isDir() ? -1 : fileName.lastIndexOf(QLatin1Char('.'));
    const QString base = dot > 0 ? fileName.left(dot) : fileName;
    const QString suffix = dot > 0 ? fileName.mid(dot + 1) : QString();
    for (int n = 2; n <= MaxUniqueNameTries; ++n) {
        QString name = QStringLiteral("%1 (%2)").arg(base).arg(n);
        if (!suffix.isEmpty()) {
            name += QLatin1Char('.') + suffix;
        }
        const QString candidate = QDir(dir).filePath(name);
        if (!pathExists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

LocalFileOperation::LocalFileOperation(Kind kind, QList<TransferItem> items, QObject *parent)
    : FileOperation(parent)
    , m_kind(kind)
    , m_items(std::move(items))
{
}

void LocalFileOperation::execute()
{
    if (m_kind == Kind::Delete) {
        executeDelete();
    } else {
        executeTransfer();
    }
}

void LocalFileOperation::executeDelete()
{
    m_total = m_items.size();
    for (const TransferItem &item : std::as_const(m_items)) {
        if (isCancelled()) {
            return;
        }
        reportProgress(m_done, m_total, item.source, true);
        for (int attempt = 0; attempt <= MaxRetries; ++attempt) {
            if (QFile::moveToTrash(item.source)) {
                ++m_result.processed;
                break;
            }
            const ErrorAction action = attempt < MaxRetries
                ? onError(item.source, QStringLiteral("Could not move to the recycle bin."))
                : ErrorAction::Skip;
            if (action == ErrorAction::Abort) {
                return;
            }
            if (action == ErrorAction::Skip) {
                break;
            }
        }
        ++m_done;
    }
    reportProgress(m_done, m_total, QString(), true);
}

void LocalFileOperation::executeTransfer()
{
    QList<Job> jobs;
    for (const TransferItem &item : std::as_const(m_items)) {
        if (isCancelled()) {
            return;
        }
        if (!validate(item)) {
            continue;
        }
        // A same-volume move of a whole item is a single atomic rename.
        if (m_kind == Kind::Move && (!pathExists(item.target) || samePath(item.source, item.target))) {
            if (QDir().rename(item.source, item.target)) {
                ++m_result.processed;
                continue;
            }
            if (samePath(item.source, item.target)) {
                recordSkip(item.source, QStringLiteral("Cannot rename."));
                continue;
            }
        }
        expand(item.source, item.target, 0, jobs);
    }

    for (const Job &job : std::as_const(jobs)) {
        if (isCancelled()) {
            return;
        }
        if (isUnderSkippedDir(job.source)) {
            continue;
        }
        const Step step = job.isDir ? makeDir(job) : transferFile(job);
        if (step == Step::Aborted) {
            return;
        }
        if (step == Step::Skipped && job.isDir) {
            m_skippedDirs.append(job.source);
        }
    }

    if (m_kind == Kind::Move) {
        removeEmptySourceDirs(jobs);
    }
    reportProgress(m_done, m_total, QString(), true);
}

bool LocalFileOperation::validate(const TransferItem &item)
{
    if (!pathExists(item.source)) {
        recordSkip(item.source, QStringLiteral("No longer exists."));
        return false;
    }
    // A case-only rename is allowed on case-insensitive file systems.
    const bool caseRename = m_kind == Kind::Move && item.source != item.target;
    if (samePath(item.source, item.target) && !caseRename) {
        recordSkip(item.source, QStringLiteral("Cannot copy or move a file onto itself."));
        return false;
    }
    if (QFileInfo(item.source).isDir() && isInside(item.target, item.source)) {
        recordSkip(item.source, QStringLiteral("Cannot copy or move a folder into itself."));
        return false;
    }
    return true;
}

void LocalFileOperation::expand(const QString &source, const QString &target, int depth,
                                QList<Job> &jobs)
{
    if (isCancelled()) {
        return;
    }
    const QFileInfo info(source);
    if (info.isSymLink() || info.isJunction()) {
        if (info.isDir()) {
            recordSkip(source, QStringLiteral("Links to folders are not copied."));
            return;
        }
    }
    if (!info.isDir()) {
        jobs.append({source, target, false, info.size()});
        m_total += info.size();
        return;
    }
    if (depth >= MaxDepth) {
        recordSkip(source, QStringLiteral("Folder nesting is too deep."));
        return;
    }
    jobs.append({source, target, true, 0});
    const QFileInfoList children = QDir(source).entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);
    for (const QFileInfo &child : children) {
        expand(child.filePath(), QDir(target).filePath(child.fileName()), depth + 1, jobs);
    }
}

LocalFileOperation::Step LocalFileOperation::makeDir(const Job &job)
{
    for (int attempt = 0; attempt <= MaxRetries; ++attempt) {
        const QFileInfo info(job.target);
        if (info.isDir()) {
            return Step::Done;
        }
        QString message;
        if (pathExists(job.target)) {
            message = QStringLiteral("A file with this name already exists.");
        } else if (QDir().mkdir(job.target)) {
            return Step::Done;
        } else {
            message = QStringLiteral("Cannot create folder.");
        }
        const ErrorAction action = attempt < MaxRetries ? onError(job.target, message) : ErrorAction::Skip;
        if (action == ErrorAction::Abort) {
            return Step::Aborted;
        }
        if (action == ErrorAction::Skip) {
            return Step::Skipped;
        }
    }
    return Step::Skipped;
}

LocalFileOperation::Step LocalFileOperation::transferFile(const Job &job)
{
    const qint64 doneAtStart = m_done;
    for (int attempt = 0; attempt <= MaxRetries; ++attempt) {
        m_done = doneAtStart;
        reportProgress(m_done, m_total, job.source);

        if (!pathExists(job.source)) {
            recordSkip(job.source, QStringLiteral("No longer exists."));
            m_done = doneAtStart + job.size;
            return Step::Skipped;
        }

        const Resolution r = resolveCollision(job.source, job.target);
        if (r.step != Step::Done) {
            m_done = doneAtStart + job.size;
            return r.step;
        }

        QString error;
        bool ok = false;
        if (m_kind == Kind::Move && replaceInto(job.source, r.target, r.replace, nullptr)) {
            ok = true;
        } else {
            const QString part = r.target + PartSuffix;
            ok = copyData(job.source, part, &error) && replaceInto(part, r.target, r.replace, &error);
            if (!ok) {
                // A leftover part file is removed again by the next attempt.
                (void)removeFile(part);
            }
            if (ok && m_kind == Kind::Move && !removeFile(job.source)) {
                recordSkip(job.source, QStringLiteral("Copied, but the source could not be removed."));
                m_done = doneAtStart + job.size;
                return Step::Skipped;
            }
        }

        if (ok) {
            ++m_result.processed;
            m_done = doneAtStart + job.size;
            return Step::Done;
        }
        if (isCancelled()) {
            return Step::Aborted;
        }
        const ErrorAction action = attempt < MaxRetries ? onError(job.source, error) : ErrorAction::Skip;
        if (action == ErrorAction::Abort) {
            return Step::Aborted;
        }
        if (action == ErrorAction::Skip) {
            m_done = doneAtStart + job.size;
            return Step::Skipped;
        }
    }
    return Step::Skipped;
}

LocalFileOperation::Resolution LocalFileOperation::resolveCollision(const QString &source,
                                                                   const QString &target)
{
    Resolution r;
    r.target = target;
    if (!pathExists(target)) {
        return r;
    }
    if (QFileInfo(target).isDir()) {
        recordSkip(target, QStringLiteral("A folder with this name already exists."));
        r.step = Step::Skipped;
        return r;
    }

    Answer answer = Answer::Abort;
    if (m_overwriteAll) {
        answer = Answer::Overwrite;
    } else if (m_skipAllCollisions) {
        answer = Answer::Skip;
    } else {
        answer = ask({Query::Kind::Collision, source, target, QString()});
    }

    switch (answer) {
    case Answer::OverwriteAll:
        m_overwriteAll = true;
        [[fallthrough]];
    case Answer::Overwrite:
        r.replace = true;
        return r;
    case Answer::SkipAll:
        m_skipAllCollisions = true;
        [[fallthrough]];
    case Answer::Skip:
        ++m_result.skipped;
        r.step = Step::Skipped;
        return r;
    case Answer::Rename:
        r.target = uniqueTargetName(target);
        if (r.target.isEmpty()) {
            recordSkip(target, QStringLiteral("No free name found."));
            r.step = Step::Skipped;
        }
        return r;
    case Answer::Retry:
    case Answer::Abort:
        break;
    }
    r.step = Step::Aborted;
    return r;
}

bool LocalFileOperation::copyData(const QString &source, const QString &target, QString *error)
{
    if (pathExists(target) && !removeFile(target)) {
        *error = QStringLiteral("Cannot remove stale temporary file %1.").arg(QDir::toNativeSeparators(target));
        return false;
    }
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) {
        *error = in.errorString();
        return false;
    }
    QFile out(target);
    if (!out.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        *error = out.errorString();
        return false;
    }

    const qint64 expected = in.size();
    QByteArray buffer(static_cast<qsizetype>(std::min(ChunkSize, std::max<qint64>(expected, 1))),
                      Qt::Uninitialized);
    qint64 copied = 0;
    // Each pass copies at least one byte or exits, so this ends after at most expected passes.
    while (copied < expected) {
        if (isCancelled()) {
            *error = QStringLiteral("Cancelled.");
            return false;
        }
        const qint64 want = std::min<qint64>(buffer.size(), expected - copied);
        const qint64 got = in.read(buffer.data(), want);
        if (got <= 0) {
            *error = got < 0 ? in.errorString() : QStringLiteral("File became shorter while copying.");
            return false;
        }
        if (out.write(buffer.constData(), got) != got) {
            *error = out.errorString();
            return false;
        }
        copied += got;
        m_done += got;
        reportProgress(m_done, m_total, source);
    }

    if (!out.flush()) {
        *error = out.errorString();
        return false;
    }
    // Best effort: some targets (FAT, network shares) cannot store the time exactly.
    (void)out.setFileTime(in.fileTime(QFileDevice::FileModificationTime), QFileDevice::FileModificationTime);
    out.close();
    if (out.error() != QFileDevice::NoError) {
        *error = out.errorString();
        return false;
    }
    // Best effort: permission bits are not supported on every target file system.
    (void)QFile::setPermissions(target, in.permissions());
    return true;
}

bool LocalFileOperation::replaceInto(const QString &from, const QString &target, bool replace,
                                     QString *error)
{
    if (!replace) {
        if (QDir().rename(from, target)) {
            return true;
        }
        if (error != nullptr) {
            *error = QStringLiteral("Cannot create %1.").arg(QDir::toNativeSeparators(target));
        }
        return false;
    }

    // Swap through a backup so the old target survives if the final rename fails.
    const QString backup = target + BackupSuffix;
    if (pathExists(backup) && !removeFile(backup)) {
        if (error != nullptr) {
            *error = QStringLiteral("Cannot remove stale backup %1.").arg(QDir::toNativeSeparators(backup));
        }
        return false;
    }
    if (!QDir().rename(target, backup)) {
        if (error != nullptr) {
            *error = QStringLiteral("Cannot replace %1.").arg(QDir::toNativeSeparators(target));
        }
        return false;
    }
    if (!QDir().rename(from, target)) {
        const bool restored = QDir().rename(backup, target);
        if (error != nullptr) {
            *error = restored
                ? QStringLiteral("Cannot replace %1.").arg(QDir::toNativeSeparators(target))
                : QStringLiteral("Cannot replace %1. The original is kept as %2.")
                      .arg(QDir::toNativeSeparators(target), QDir::toNativeSeparators(backup));
        }
        return false;
    }
    if (!removeFile(backup)) {
        m_result.errors.append(QStringLiteral("%1: replaced, but the backup could not be removed.")
                                   .arg(QDir::toNativeSeparators(backup)));
    }
    return true;
}

void LocalFileOperation::removeEmptySourceDirs(const QList<Job> &jobs)
{
    // Jobs are in pre-order, so reverse order visits children before parents.
    for (auto it = jobs.crbegin(); it != jobs.crend(); ++it) {
        if (!it->isDir) {
            continue;
        }
        const QDir dir(it->source);
        if (!dir.exists()) {
            continue;
        }
        // A non-empty folder still holds skipped files and is kept on purpose.
        if (dir.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)
            && !QDir().rmdir(it->source)) {
            m_result.errors.append(QStringLiteral("%1: moved, but the empty folder could not be removed.")
                                       .arg(QDir::toNativeSeparators(it->source)));
        }
    }
}

bool LocalFileOperation::isUnderSkippedDir(const QString &path) const
{
    for (const QString &dir : m_skippedDirs) {
        if (isInside(path, dir)) {
            return true;
        }
    }
    return false;
}

LocalFileOperation::ErrorAction LocalFileOperation::onError(const QString &path, const QString &message)
{
    if (isCancelled()) {
        return ErrorAction::Abort;
    }
    if (m_skipAllErrors) {
        recordSkip(path, message);
        return ErrorAction::Skip;
    }
    switch (ask({Query::Kind::Error, path, QString(), message})) {
    case Answer::Retry:
        return ErrorAction::Retry;
    case Answer::SkipAll:
        m_skipAllErrors = true;
        [[fallthrough]];
    case Answer::Skip:
        recordSkip(path, message);
        return ErrorAction::Skip;
    case Answer::Overwrite:
    case Answer::OverwriteAll:
    case Answer::Rename:
    case Answer::Abort:
        break;
    }
    return ErrorAction::Abort;
}

} // namespace bifrons
