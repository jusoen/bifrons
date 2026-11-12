#include "core/transfer.h"

#include <QDir>
#include <QFileInfo>

#include "core/localfilesource.h"

namespace bifrons {

TransferPlan planTransfer(const QStringList &sources, const QString &destination,
                          const QString &currentDir)
{
    TransferPlan plan;
    if (sources.isEmpty()) {
        plan.error = QStringLiteral("Nothing selected.");
        return plan;
    }
    const QString trimmed = destination.trimmed();
    if (trimmed.isEmpty()) {
        plan.error = QStringLiteral("No destination given.");
        return plan;
    }

    const QString raw = QDir::fromNativeSeparators(trimmed);
    const bool endsWithSlash = raw.endsWith(QLatin1Char('/'));
    const QString dest = LocalFileSource::normalise(
        QDir::isAbsolutePath(raw) ? raw : QDir(currentDir).filePath(raw));
    const QFileInfo destInfo(dest);

    if (destInfo.isDir() || endsWithSlash || sources.size() > 1) {
        if (destInfo.exists() && !destInfo.isDir()) {
            plan.error = QStringLiteral("Destination is a file: %1").arg(QDir::toNativeSeparators(dest));
            return plan;
        }
        if (!destInfo.exists()) {
            plan.createDir = dest;
        }
        for (const QString &src : sources) {
            plan.items.append({src, QDir(dest).filePath(QFileInfo(src).fileName())});
        }
    } else {
        const QString parent = destInfo.absolutePath();
        if (!QFileInfo(parent).isDir()) {
            plan.createDir = LocalFileSource::normalise(parent);
        }
        plan.items.append({sources.first(), dest});
    }
    plan.ok = true;
    return plan;
}

QString transferTargetProblem(const QStringList &sources, const QString &targetDir)
{
    if (sources.isEmpty()) {
        return QStringLiteral("Nothing to copy or move.");
    }
    if (targetDir.isEmpty()) {
        return QStringLiteral("No destination folder.");
    }
    const QString target = QDir::cleanPath(QDir::fromNativeSeparators(targetDir));
    bool allFromTarget = true;
    for (const QString &raw : sources) {
        const QString source = QDir::cleanPath(QDir::fromNativeSeparators(raw));
        if (target.compare(source, Qt::CaseInsensitive) == 0
            || target.startsWith(source + QLatin1Char('/'), Qt::CaseInsensitive)) {
            return QStringLiteral("A folder cannot go inside itself: %1").arg(QDir::toNativeSeparators(source));
        }
        if (QFileInfo(source).absolutePath().compare(target, Qt::CaseInsensitive) != 0) {
            allFromTarget = false;
        }
    }
    if (allFromTarget) {
        return QStringLiteral("The items are already in this folder.");
    }
    return QString();
}

} // namespace bifrons
