#pragma once

#include <QString>

#include "core/fileitem.h"

namespace bifrons {

struct ListResult
{
    bool ok = false;
    FileItemList items;
    QString error;
};

// Browsable file tree with '/' paths. Implementations must be thread-safe.
class FileSource
{
public:
    virtual ~FileSource() = default;

    virtual ListResult list(const QString &dir) const = 0;
    virtual bool isDirectory(const QString &path) const = 0;
    // Parent of path, or an empty string when path is a root.
    virtual QString parentPath(const QString &path) const = 0;
    virtual QString childPath(const QString &dir, const QString &name) const = 0;
    virtual bool makePath(const QString &path, QString *error) const = 0;
    // Path formatted for display to the user.
    virtual QString displayPath(const QString &path) const = 0;
};

} // namespace bifrons
