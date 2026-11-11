#include "core/localfilesource.h"

#include <QDir>

namespace dcpp {

namespace {

QString attributeString(const QFileInfo &info)
{
    QString s;
    s += info.isSymLink() ? QLatin1Char('l') : (info.isDir() ? QLatin1Char('d') : QLatin1Char('-'));
#if defined(Q_OS_WIN)
    s += info.isWritable() ? QLatin1Char('-') : QLatin1Char('r');
    s += info.isHidden() ? QLatin1Char('h') : QLatin1Char('-');
#else
    const QFile::Permissions p = info.permissions();
    s += (p & QFile::ReadOwner) ? QLatin1Char('r') : QLatin1Char('-');
    s += (p & QFile::WriteOwner) ? QLatin1Char('w') : QLatin1Char('-');
    s += (p & QFile::ExeOwner) ? QLatin1Char('x') : QLatin1Char('-');
    s += (p & QFile::ReadGroup) ? QLatin1Char('r') : QLatin1Char('-');
    s += (p & QFile::WriteGroup) ? QLatin1Char('w') : QLatin1Char('-');
    s += (p & QFile::ExeGroup) ? QLatin1Char('x') : QLatin1Char('-');
    s += (p & QFile::ReadOther) ? QLatin1Char('r') : QLatin1Char('-');
    s += (p & QFile::WriteOther) ? QLatin1Char('w') : QLatin1Char('-');
    s += (p & QFile::ExeOther) ? QLatin1Char('x') : QLatin1Char('-');
#endif
    return s;
}

} // namespace

FileItem LocalFileSource::itemFromInfo(const QFileInfo &info)
{
    FileItem item;
    item.name = info.fileName();
    item.isDir = info.isDir();
    item.isSymLink = info.isSymLink();
    item.isHidden = info.isHidden();
    item.size = item.isDir ? 0 : info.size();
    item.modified = info.lastModified();
    item.attributes = attributeString(info);
    return item;
}

ListResult LocalFileSource::list(const QString &dir) const
{
    ListResult result;
    const QDir d(dir);
    if (!d.exists()) {
        result.error = QStringLiteral("Directory does not exist: %1").arg(displayPath(dir));
        return result;
    }
    if (!d.isReadable()) {
        result.error = QStringLiteral("Access denied: %1").arg(displayPath(dir));
        return result;
    }
    const QFileInfoList infos = d.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::NoSort);
    result.items.reserve(infos.size());
    for (const QFileInfo &info : infos) {
        result.items.append(itemFromInfo(info));
    }
    result.ok = true;
    return result;
}

bool LocalFileSource::isDirectory(const QString &path) const
{
    return QFileInfo(path).isDir();
}

QString LocalFileSource::parentPath(const QString &path) const
{
    const QString current = normalise(path);
    const QString parent = normalise(QFileInfo(current).absolutePath());
    return parent == current ? QString() : parent;
}

QString LocalFileSource::childPath(const QString &dir, const QString &name) const
{
    return normalise(QDir(dir).filePath(name));
}

bool LocalFileSource::makePath(const QString &path, QString *error) const
{
    if (QDir().mkpath(path)) {
        return true;
    }
    if (error != nullptr) {
        *error = QStringLiteral("Cannot create directory: %1").arg(displayPath(path));
    }
    return false;
}

QString LocalFileSource::displayPath(const QString &path) const
{
    return QDir::toNativeSeparators(path);
}

QString LocalFileSource::normalise(const QString &path)
{
    return QDir::cleanPath(QDir(QDir::fromNativeSeparators(path)).absolutePath());
}

QString LocalFileSource::nearestExistingDir(const QString &path)
{
    if (path.isEmpty()) {
        return normalise(QDir::homePath());
    }
    const LocalFileSource fs;
    QString current = normalise(path);
    // Each step strictly shortens the path, so this ends at the root at the latest.
    while (!current.isEmpty()) {
        if (QFileInfo(current).isDir()) {
            return current;
        }
        current = fs.parentPath(current);
    }
    return normalise(QDir::homePath());
}

} // namespace dcpp
