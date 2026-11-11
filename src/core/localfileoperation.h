#pragma once

#include <QList>
#include <QStringList>

#include "core/fileoperation.h"
#include "core/transfer.h"

namespace bifrons {

// Local copy, move or delete-to-trash. Copies go via "<target>.bifrons-part".
class LocalFileOperation final : public FileOperation
{
    Q_OBJECT

public:
    enum class Kind
    {
        Copy,
        Move,
        Delete
    };

    // For Delete, only TransferItem::source is used.
    LocalFileOperation(Kind kind, QList<TransferItem> items, QObject *parent = nullptr);

    static constexpr int MaxDepth = 256;
    static constexpr int MaxRetries = 10;

protected:
    void execute() override;

private:
    struct Job
    {
        QString source;
        QString target;
        bool isDir = false;
        qint64 size = 0;
    };

    enum class Step
    {
        Done,
        Skipped,
        Aborted
    };

    enum class ErrorAction
    {
        Retry,
        Skip,
        Abort
    };

    struct Resolution
    {
        Step step = Step::Done;
        QString target;
        bool replace = false;
    };

    void executeTransfer();
    void executeDelete();
    bool validate(const TransferItem &item);
    void expand(const QString &source, const QString &target, int depth, QList<Job> &jobs);
    Step makeDir(const Job &job);
    Step transferFile(const Job &job);
    Resolution resolveCollision(const QString &source, const QString &target);
    bool copyData(const QString &source, const QString &target, QString *error);
    bool replaceInto(const QString &from, const QString &target, bool replace, QString *error);
    void removeEmptySourceDirs(const QList<Job> &jobs);
    bool isUnderSkippedDir(const QString &path) const;
    ErrorAction onError(const QString &path, const QString &message);

    Kind m_kind;
    QList<TransferItem> m_items;
    QStringList m_skippedDirs;
    qint64 m_done = 0;
    qint64 m_total = 0;
    bool m_overwriteAll = false;
    bool m_skipAllCollisions = false;
    bool m_skipAllErrors = false;
};

// "name (2).ext" style alternative that does not exist yet, or empty if none found.
QString uniqueTargetName(const QString &target);

} // namespace bifrons
