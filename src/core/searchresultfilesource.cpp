#include "core/searchresultfilesource.h"

#include <QDir>
#include <QFileInfo>

#include "core/localfilesource.h"

namespace dcpp {

SearchResultFileSource::SearchResultFileSource(QString baseDir, QStringList paths)
    : m_baseDir(LocalFileSource::normalise(baseDir))
    , m_paths(std::move(paths))
{
}

QString SearchResultFileSource::rootPath()
{
    return QStringLiteral("search:");
}

QString SearchResultFileSource::baseDir() const
{
    return m_baseDir;
}

ListResult SearchResultFileSource::list(const QString &dir) const
{
    ListResult result;
    if (dir != rootPath()) {
        result.error = QStringLiteral("Not a search result listing.");
        return result;
    }
    const QDir base(m_baseDir);
    result.items.reserve(m_paths.size());
    for (const QString &path : m_paths) {
        const QFileInfo info(path);
        // Files moved or deleted since the search drop out of the listing.
        if (!info.exists() && !info.isSymLink()) {
            continue;
        }
        FileItem item = LocalFileSource::itemFromInfo(info);
        item.name = base.relativeFilePath(path);
        result.items.append(item);
    }
    result.ok = true;
    return result;
}

bool SearchResultFileSource::isDirectory(const QString &path) const
{
    return path == rootPath() || QFileInfo(path).isDir();
}

QString SearchResultFileSource::parentPath(const QString &) const
{
    return QString();
}

QString SearchResultFileSource::childPath(const QString &, const QString &name) const
{
    return LocalFileSource::normalise(QDir(m_baseDir).filePath(name));
}

bool SearchResultFileSource::makePath(const QString &, QString *error) const
{
    if (error != nullptr) {
        *error = QStringLiteral("Folders cannot be created in search results.");
    }
    return false;
}

QString SearchResultFileSource::displayPath(const QString &) const
{
    return QStringLiteral("Search results in %1").arg(QDir::toNativeSeparators(m_baseDir));
}

} // namespace dcpp
