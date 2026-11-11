#pragma once

#include <QFileInfo>

#include "core/filesource.h"

namespace dcpp {

class LocalFileSource final : public FileSource
{
public:
    ListResult list(const QString &dir) const override;
    bool isDirectory(const QString &path) const override;
    QString parentPath(const QString &path) const override;
    QString childPath(const QString &dir, const QString &name) const override;
    bool makePath(const QString &path, QString *error) const override;
    QString displayPath(const QString &path) const override;

    // Cleaned absolute path with '/' separators.
    static QString normalise(const QString &path);
    // Closest existing directory at or above path, or the home directory if none.
    static QString nearestExistingDir(const QString &path);
    static FileItem itemFromInfo(const QFileInfo &info);
};

} // namespace dcpp
