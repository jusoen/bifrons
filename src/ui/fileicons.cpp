#include "ui/fileicons.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QPixmap>
#include <QStyle>

#include "core/localfilesource.h"

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#include <objbase.h>
#include <shellapi.h>
#endif

namespace bifrons {

namespace {

// Per-file icons kept in memory. Each is a small pixmap, so this costs a few MB at most.
constexpr int MaxPathIcons = 2000;
// Background readers. Few, so a folder of programs on a slow share cannot starve folder listing.
constexpr int IconThreads = 2;

// Extensions whose icon is stored in each file rather than registered for the type.
bool hasOwnIcon(const QString &suffix)
{
#if defined(Q_OS_WIN)
    static const QSet<QString> own = {QStringLiteral("exe"), QStringLiteral("lnk"), QStringLiteral("ico"),
                                      QStringLiteral("cur"), QStringLiteral("ani"), QStringLiteral("scr")};
    return own.contains(suffix);
#else
    Q_UNUSED(suffix);
    return false;
#endif
}

#if defined(Q_OS_WIN)
// Small shell icon for path. With useAttributes the file is never opened, so only the extension
// (or, with FILE_ATTRIBUTE_DIRECTORY, "a folder") matters.
QImage shellIcon(const QString &path, bool useAttributes, DWORD attributes = FILE_ATTRIBUTE_NORMAL)
{
    SHFILEINFOW info = {};
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    UINT flags = SHGFI_ICON | SHGFI_SMALLICON;
    if (useAttributes) {
        flags |= SHGFI_USEFILEATTRIBUTES;
    }
    if (SHGetFileInfoW(native.c_str(), attributes, &info, sizeof(info), flags) == 0
        || info.hIcon == nullptr) {
        return QImage();
    }
    const QImage image = QImage::fromHICON(info.hIcon);
    (void)DestroyIcon(info.hIcon); // The image holds its own copy. A failed free only leaks one handle.
    return image;
}
#endif

} // namespace

FileIcons &FileIcons::instance()
{
    static FileIcons *icons = new FileIcons(qApp);
    return *icons;
}

FileIcons::FileIcons(QObject *parent)
    : QObject(parent)
    , m_folderIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon))
    , m_genericFileIcon(QApplication::style()->standardIcon(QStyle::SP_FileIcon))
    , m_byPath(MaxPathIcons)
{
    m_pool.setMaxThreadCount(IconThreads);
#if defined(Q_OS_WIN)
    // Explorer's plain folder rather than the Qt style's, so it matches the per-folder icons.
    const QImage folder = shellIcon(QStringLiteral("folder"), true, FILE_ATTRIBUTE_DIRECTORY);
    if (!folder.isNull()) {
        m_folderIcon = QIcon(QPixmap::fromImage(folder));
    }
#endif
}

FileIcons::~FileIcons()
{
    // Drop queued reads so exit waits only for the at most IconThreads reads already running.
    m_pool.clear();
    m_pool.waitForDone();
}

QIcon FileIcons::icon(const FileItem &item, const QString &path)
{
    if (item.isDir) {
#if defined(Q_OS_WIN)
        // Reading a folder's desktop.ini over the network can stall, so shares keep the plain icon.
        if (!path.isEmpty() && !LocalFileSource::isNetworkPath(path)) {
            if (const QIcon *cached = m_byPath.object(path)) {
                return *cached;
            }
            requestFileIcon(path, true);
        }
#endif
        return m_folderIcon;
    }
    const QString suffix = item.extension().toLower();
    if (hasOwnIcon(suffix) && !path.isEmpty()) {
        if (const QIcon *cached = m_byPath.object(path)) {
            return *cached;
        }
        requestFileIcon(path, false);
    }
    return extensionIcon(suffix);
}

QIcon FileIcons::extensionIcon(const QString &suffix)
{
    const auto found = m_byExtension.constFind(suffix);
    if (found != m_byExtension.constEnd()) {
        return found.value();
    }
    QIcon icon;
    // A made-up name with the right extension: the lookup goes by type and never reads a real file.
    const QString probe = suffix.isEmpty() ? QStringLiteral("file") : QStringLiteral("file.") + suffix;
#if defined(Q_OS_WIN)
    const QImage image = shellIcon(probe, true);
    if (!image.isNull()) {
        icon = QIcon(QPixmap::fromImage(image));
    }
#else
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(probe, QMimeDatabase::MatchExtension);
    icon = QIcon::fromTheme(mime.iconName(), QIcon::fromTheme(mime.genericIconName()));
#endif
    if (icon.isNull()) {
        icon = m_genericFileIcon;
    }
    m_byExtension.insert(suffix, icon);
    return icon;
}

void FileIcons::requestFileIcon(const QString &path, bool isDir)
{
    if (m_pending.contains(path)) {
        return;
    }
    m_pending.insert(path);
    m_pool.start([this, path, isDir] {
        QImage image;
#if defined(Q_OS_WIN)
        // The shell needs COM on the calling thread to resolve shortcuts.
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        image = shellIcon(path, false);
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
#endif
        // QImage is safe to pass between threads. The pixmap is made on the UI thread.
        QMetaObject::invokeMethod(this, [this, path, image, isDir] { onFileIconLoaded(path, image, isDir); },
                                  Qt::QueuedConnection);
    });
}

void FileIcons::onFileIconLoaded(const QString &path, const QImage &image, bool isDir)
{
    m_pending.remove(path);
    // A failed read caches the fallback icon, so the item is not retried on every repaint.
    const QIcon fallback = isDir ? m_folderIcon : extensionIcon(QFileInfo(path).suffix().toLower());
    const QIcon icon = image.isNull() ? fallback : QIcon(QPixmap::fromImage(image));
    m_byPath.insert(path, new QIcon(icon));
    emit iconReady(path);
}

} // namespace bifrons
