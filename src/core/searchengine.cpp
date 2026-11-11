#include "core/searchengine.h"

#include <QDir>
#include <QFile>
#include <QStringDecoder>

namespace bifrons {

namespace {

constexpr qint64 ChunkSize = 1024 * 1024;
constexpr int FlushCount = 200;
constexpr qint64 FlushIntervalMs = 100;

} // namespace

bool fileContainsText(const QString &path, const QString &text, Qt::CaseSensitivity textCase,
                      QStringConverter::Encoding encoding, const std::atomic<bool> *cancelled,
                      QString *error)
{
    if (text.isEmpty()) {
        return true;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    QStringDecoder decoder(encoding);
    const qsizetype overlap = text.size() - 1;
    QString carry;
    // Each pass consumes at least one byte or exits, so this ends at end of file.
    while (true) {
        if (cancelled != nullptr && cancelled->load()) {
            return false;
        }
        const QByteArray chunk = file.read(ChunkSize);
        if (chunk.isEmpty()) {
            if (file.error() != QFileDevice::NoError && error != nullptr) {
                *error = file.errorString();
            }
            return false;
        }
        const QString window = carry + decoder.decode(chunk);
        if (window.contains(text, textCase)) {
            return true;
        }
        // Keep the tail so a match split across two chunks is still found.
        carry = overlap > 0 ? window.right(overlap) : QString();
    }
}

SearchEngine::SearchEngine(SearchCriteria criteria, QObject *parent)
    : QObject(parent)
    , m_criteria(std::move(criteria))
    , m_mask(m_criteria.nameMask.trimmed().isEmpty() ? QStringLiteral("*") : m_criteria.nameMask)
{
}

void SearchEngine::cancel()
{
    m_cancelled.store(true);
}

bool SearchEngine::isCancelled() const
{
    return m_cancelled.load();
}

SearchSummary SearchEngine::summary() const
{
    return m_summary;
}

bool SearchEngine::matchesAttributes(const QFileInfo &info, const SearchCriteria &criteria, const FileMask &mask)
{
    if (!mask.matches(info.fileName())) {
        return false;
    }
    const bool isDir = info.isDir();
    if (isDir && (!criteria.text.isEmpty() || criteria.minSize || criteria.maxSize)) {
        return false;
    }
    if (criteria.minSize && info.size() < *criteria.minSize) {
        return false;
    }
    if (criteria.maxSize && info.size() > *criteria.maxSize) {
        return false;
    }
    const QDateTime modified = info.lastModified();
    if (criteria.modifiedAfter && modified < *criteria.modifiedAfter) {
        return false;
    }
    if (criteria.modifiedBefore && modified > *criteria.modifiedBefore) {
        return false;
    }
    return true;
}

void SearchEngine::run()
{
    m_summary = SearchSummary{};
    m_pending.clear();
    m_flushTimer.start();
    if (!m_mask.isValid()) {
        m_summary.errors.append(QStringLiteral("Invalid file name mask."));
    } else if (!QFileInfo(m_criteria.startDir).isDir()) {
        m_summary.errors.append(QStringLiteral("Folder does not exist: %1")
                                    .arg(QDir::toNativeSeparators(m_criteria.startDir)));
    } else {
        searchDir(m_criteria.startDir, 0);
    }
    flush(true);
    m_summary.cancelled = isCancelled();
    emit finished();
}

void SearchEngine::searchDir(const QString &dir, int depth)
{
    if (isCancelled() || m_summary.truncated) {
        return;
    }
    emit progress(dir, m_summary.scanned);
    const QDir d(dir);
    if (!d.isReadable()) {
        m_summary.errors.append(QStringLiteral("Access denied: %1").arg(QDir::toNativeSeparators(dir)));
        return;
    }
    const QFileInfoList entries =
        d.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);
    for (const QFileInfo &info : entries) {
        if (isCancelled() || m_summary.truncated) {
            return;
        }
        consider(info);
        // Links to folders are not followed, so a link cycle cannot recurse forever.
        const bool descend = info.isDir() && !info.isSymLink() && !info.isJunction();
        if (descend) {
            if (depth + 1 >= MaxDepth) {
                m_summary.errors.append(QStringLiteral("Too deep, not searched: %1")
                                            .arg(QDir::toNativeSeparators(info.filePath())));
            } else {
                searchDir(info.filePath(), depth + 1);
            }
        }
    }
}

void SearchEngine::consider(const QFileInfo &info)
{
    ++m_summary.scanned;
    if (!matchesAttributes(info, m_criteria, m_mask)) {
        return;
    }
    if (!m_criteria.text.isEmpty()) {
        QString error;
        if (!fileContainsText(info.filePath(), m_criteria.text, m_criteria.textCase, m_criteria.encoding,
                              &m_cancelled, &error)) {
            if (!error.isEmpty()) {
                m_summary.errors.append(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(info.filePath()), error));
            }
            return;
        }
    }
    m_pending.append(info.filePath());
    ++m_summary.found;
    if (m_summary.found >= MaxResults) {
        m_summary.truncated = true;
    }
    flush(false);
}

void SearchEngine::flush(bool force)
{
    if (m_pending.isEmpty()) {
        return;
    }
    if (!force && m_pending.size() < FlushCount && m_flushTimer.elapsed() < FlushIntervalMs) {
        return;
    }
    emit found(m_pending);
    m_pending.clear();
    m_flushTimer.restart();
}

} // namespace bifrons
