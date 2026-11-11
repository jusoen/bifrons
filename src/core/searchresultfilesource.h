#pragma once

#include <QStringList>

#include "core/filesource.h"

namespace dcpp {

// Immutable flat listing of search results, named relative to baseDir.
class SearchResultFileSource final : public FileSource
{
public:
    SearchResultFileSource(QString baseDir, QStringList paths);

    // The single listable path of this source.
    static QString rootPath();
    QString baseDir() const;

    ListResult list(const QString &dir) const override;
    bool isDirectory(const QString &path) const override;
    QString parentPath(const QString &path) const override;
    QString childPath(const QString &dir, const QString &name) const override;
    bool makePath(const QString &path, QString *error) const override;
    QString displayPath(const QString &path) const override;

private:
    QString m_baseDir;
    QStringList m_paths;
};

} // namespace dcpp
