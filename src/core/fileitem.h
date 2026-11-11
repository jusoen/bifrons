#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace dcpp {

// One directory entry as shown in a panel.
struct FileItem
{
    QString name;
    qint64 size = 0;
    QDateTime modified;
    bool isDir = false;
    bool isSymLink = false;
    bool isHidden = false;
    QString attributes;

    // Suffix after the last dot for files. Empty for directories and dot-files.
    QString extension() const;
    // Name without the extension.
    QString baseName() const;
};

using FileItemList = QList<FileItem>;

} // namespace dcpp
