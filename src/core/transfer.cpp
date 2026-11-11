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

} // namespace bifrons
