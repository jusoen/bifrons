#pragma once

#include <QCache>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QThreadPool>

#include "core/fileitem.h"

namespace bifrons {

// Shell icons for files, as Explorer shows them. Shared by all panels, used on the UI thread only.
//
// Most files get the icon of their extension, looked up once per extension without touching the file.
// Programs, shortcuts, icon files and local folders carry their own icon (a folder's comes from its
// special role or desktop.ini): those are read on a small background pool and announced through
// iconReady(). Until then, and if the read fails, the extension or plain folder icon is shown.
class FileIcons : public QObject
{
    Q_OBJECT

public:
    static FileIcons &instance();

    // path is the item's full path, used only for files whose icon lives in the file itself.
    QIcon icon(const FileItem &item, const QString &path);

signals:
    // A per-file icon finished loading. Panels showing path should repaint that row.
    void iconReady(const QString &path);

private:
    explicit FileIcons(QObject *parent = nullptr);
    ~FileIcons() override;

    QIcon extensionIcon(const QString &suffix);
    void requestFileIcon(const QString &path, bool isDir);
    void onFileIconLoaded(const QString &path, const QImage &image, bool isDir);

    QIcon m_folderIcon;
    QIcon m_genericFileIcon;
    QHash<QString, QIcon> m_byExtension;
    // Per-file icons by full path. Bounded so browsing many program folders cannot grow it forever.
    QCache<QString, QIcon> m_byPath;
    QSet<QString> m_pending;
    QThreadPool m_pool;
};

} // namespace bifrons
