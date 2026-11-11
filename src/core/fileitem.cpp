#include "core/fileitem.h"

namespace dcpp {

namespace {

// Index of the extension dot, or -1 when there is no extension.
qsizetype extensionDot(const FileItem &item)
{
    if (item.isDir) {
        return -1;
    }
    const qsizetype dot = item.name.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == item.name.size() - 1) {
        return -1;
    }
    return dot;
}

} // namespace

QString FileItem::extension() const
{
    const qsizetype dot = extensionDot(*this);
    return dot < 0 ? QString() : name.mid(dot + 1);
}

QString FileItem::baseName() const
{
    const qsizetype dot = extensionDot(*this);
    return dot < 0 ? name : name.left(dot);
}

} // namespace dcpp
