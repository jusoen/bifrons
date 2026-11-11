#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QObject>
#include <QStringConverter>
#include <QStringList>

#include <atomic>
#include <optional>

#include "core/filemask.h"

namespace bifrons {

struct SearchCriteria
{
    QString startDir;
    QString nameMask = QStringLiteral("*");
    // Empty means no content search. Folders never match a content search.
    QString text;
    Qt::CaseSensitivity textCase = Qt::CaseInsensitive;
    QStringConverter::Encoding encoding = QStringConverter::Utf8;
    std::optional<qint64> minSize;
    std::optional<qint64> maxSize;
    std::optional<QDateTime> modifiedAfter;
    std::optional<QDateTime> modifiedBefore;
};

struct SearchSummary
{
    int found = 0;
    int scanned = 0;
    bool cancelled = false;
    bool truncated = false;
    QStringList errors;
};

// Recursive file search. run() executes on the calling thread.
class SearchEngine : public QObject
{
    Q_OBJECT

public:
    static constexpr int MaxResults = 100000;
    static constexpr int MaxDepth = 256;

    explicit SearchEngine(SearchCriteria criteria, QObject *parent = nullptr);

    // Thread-safe.
    void cancel();
    bool isCancelled() const;
    // Valid once finished() has been emitted.
    SearchSummary summary() const;

    // Name, size and date tests only (no content).
    static bool matchesAttributes(const QFileInfo &info, const SearchCriteria &criteria, const FileMask &mask);

public slots:
    void run();

signals:
    // Batches of matching full paths.
    void found(const QStringList &paths);
    void progress(const QString &currentDir, int scanned);
    void finished();

private:
    void searchDir(const QString &dir, int depth);
    void consider(const QFileInfo &info);
    void flush(bool force);

    SearchCriteria m_criteria;
    FileMask m_mask;
    SearchSummary m_summary;
    QStringList m_pending;
    QElapsedTimer m_flushTimer;
    std::atomic<bool> m_cancelled{false};
};

// True if the file contains text in the given encoding. Reads in chunks and stops early on cancel.
bool fileContainsText(const QString &path, const QString &text, Qt::CaseSensitivity textCase,
                      QStringConverter::Encoding encoding, const std::atomic<bool> *cancelled,
                      QString *error);

} // namespace bifrons
