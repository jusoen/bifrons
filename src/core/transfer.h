#pragma once

#include <QList>
#include <QString>

namespace dcpp {

// One top-level source and the full path it is copied or moved to.
struct TransferItem
{
    QString source;
    QString target;
};

struct TransferPlan
{
    bool ok = false;
    QList<TransferItem> items;
    // Directory to create before the transfer starts (empty when none is needed).
    QString createDir;
    QString error;
};

// Maps a copy/move dialog destination to per-item targets (DC semantics, see tst_transfer).
TransferPlan planTransfer(const QStringList &sources, const QString &destination,
                          const QString &currentDir);

} // namespace dcpp
