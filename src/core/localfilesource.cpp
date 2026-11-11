#include "core/localfilesource.h"

#include <QDir>

#include <string>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#include <lm.h>
#endif

namespace bifrons {

namespace {

// Disk shares of a server. Hidden ("$") shares are marked hidden so the hidden-files toggle shows them.
// Runs on the listing thread. An unreachable server blocks for the OS SMB timeout, then fails.
ListResult listServerShares(const QString &server)
{
    ListResult result;
    const QString native = QDir::toNativeSeparators(server);
#if defined(Q_OS_WIN)
    const std::wstring name = native.toStdWString();
    DWORD resume = 0;
    // MAX_PREFERRED_LENGTH normally returns everything in one pass. The cap bounds a misbehaving server.
    constexpr int MaxPasses = 64;
    for (int pass = 0; pass < MaxPasses; ++pass) {
        LPBYTE buffer = nullptr;
        DWORD read = 0;
        DWORD total = 0;
        const NET_API_STATUS status = NetShareEnum(const_cast<LPWSTR>(name.c_str()), 1, &buffer,
                                                   MAX_PREFERRED_LENGTH, &read, &total, &resume);
        if (status != NERR_Success && status != ERROR_MORE_DATA) {
            if (buffer != nullptr) {
                (void)NetApiBufferFree(buffer); // Nothing useful to do if freeing fails.
            }
            result.items.clear();
            result.error = QStringLiteral("Cannot list shares on %1: %2")
                               .arg(native, qt_error_string(static_cast<int>(status)));
            return result;
        }
        const auto *shares = reinterpret_cast<const SHARE_INFO_1 *>(buffer);
        for (DWORD i = 0; i < read; ++i) {
            if ((shares[i].shi1_type & STYPE_MASK) != STYPE_DISKTREE) {
                continue;
            }
            FileItem item;
            item.name = QString::fromWCharArray(shares[i].shi1_netname);
            item.isDir = true;
            item.isHidden = (shares[i].shi1_type & STYPE_SPECIAL) != 0 || item.name.endsWith(QLatin1Char('$'));
            item.attributes = item.isHidden ? QStringLiteral("d-h") : QStringLiteral("d--");
            result.items.append(item);
        }
        if (buffer != nullptr) {
            (void)NetApiBufferFree(buffer); // Nothing useful to do if freeing fails.
        }
        if (status == NERR_Success) {
            result.ok = true;
            return result;
        }
    }
    result.items.clear();
    result.error = QStringLiteral("Cannot list shares on %1: too many shares").arg(native);
#else
    result.error = QStringLiteral("Network servers cannot be listed: %1").arg(native);
#endif
    return result;
}

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
    if (isUncServer(dir)) {
        return listServerShares(normalise(dir));
    }
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
    // Qt reports "/" as a server's parent, which on Windows is the current drive.
    if (isUncServer(current)) {
        return QString();
    }
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

bool LocalFileSource::isUncServer(const QString &path)
{
#if defined(Q_OS_WIN)
    const QString p = QDir::fromNativeSeparators(path);
    if (!p.startsWith(QLatin1String("//"))) {
        return false;
    }
    QString rest = p.mid(2);
    if (rest.endsWith(QLatin1Char('/'))) {
        rest.chop(1);
    }
    return !rest.isEmpty() && !rest.contains(QLatin1Char('/'));
#else
    Q_UNUSED(path);
    return false;
#endif
}

bool LocalFileSource::isNetworkPath(const QString &path)
{
#if defined(Q_OS_WIN)
    const QString p = QDir::fromNativeSeparators(path);
    if (p.startsWith(QLatin1String("//"))) {
        return true;
    }
    if (p.size() >= 2 && p.at(1) == QLatin1Char(':') && p.at(0).isLetter()) {
        // Reads the local drive mapping table, so a disconnected mapped drive still answers at once.
        const wchar_t root[] = {static_cast<wchar_t>(p.at(0).unicode()), L':', L'\\', L'\0'};
        return GetDriveTypeW(root) == DRIVE_REMOTE;
    }
    return false;
#else
    Q_UNUSED(path);
    return false;
#endif
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

} // namespace bifrons
