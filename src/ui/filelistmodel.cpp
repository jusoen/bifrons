#include "ui/filelistmodel.h"

#include <QApplication>
#include <QBrush>
#include <QLocale>
#include <QStyle>

namespace dcpp {

FileListModel::FileListModel(QObject *parent)
    : QAbstractTableModel(parent)
    , m_dirIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon))
    , m_fileIcon(QApplication::style()->standardIcon(QStyle::SP_FileIcon))
{
}

void FileListModel::setItems(FileItemList items, bool showParent, bool keepMarks)
{
    beginResetModel();
    m_items = std::move(items);
    m_showParent = showParent;
    sortFileItems(m_items, m_sortColumn, m_sortOrder);
    if (keepMarks) {
        QSet<QString> still;
        for (const FileItem &item : std::as_const(m_items)) {
            if (m_marked.contains(item.name)) {
                still.insert(item.name);
            }
        }
        m_marked = std::move(still);
    } else {
        m_marked.clear();
    }
    endResetModel();
    emit marksChanged();
}

int FileListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_items.size()) + (m_showParent ? 1 : 0);
}

int FileListModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(SortColumn::Count);
}

QVariant FileListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }
    const auto column = static_cast<SortColumn>(index.column());

    if (isParentRow(index.row())) {
        if (column == SortColumn::Name && role == Qt::DisplayRole) {
            return QStringLiteral("..");
        }
        if (column == SortColumn::Name && role == Qt::DecorationRole) {
            return m_dirIcon;
        }
        return QVariant();
    }

    const FileItem *item = itemAt(index.row());
    if (item == nullptr) {
        return QVariant();
    }

    switch (role) {
    case Qt::DisplayRole:
        switch (column) {
        case SortColumn::Name:
            return item->isDir ? item->name : item->baseName();
        case SortColumn::Extension:
            return item->extension();
        case SortColumn::Size:
            return item->isDir ? QStringLiteral("<DIR>") : QLocale().toString(item->size);
        case SortColumn::Modified:
            return QLocale().toString(item->modified, QLocale::ShortFormat);
        case SortColumn::Attributes:
            return item->attributes;
        case SortColumn::Count:
            break;
        }
        break;
    case Qt::DecorationRole:
        if (column == SortColumn::Name) {
            return item->isDir ? m_dirIcon : m_fileIcon;
        }
        break;
    case Qt::ForegroundRole:
        if (m_marked.contains(item->name)) {
            return QBrush(Qt::red);
        }
        if (item->isHidden) {
            return QBrush(Qt::gray);
        }
        break;
    case Qt::TextAlignmentRole:
        if (column == SortColumn::Size) {
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        }
        break;
    default:
        break;
    }
    return QVariant();
}

QVariant FileListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }
    switch (static_cast<SortColumn>(section)) {
    case SortColumn::Name:
        return tr("Name");
    case SortColumn::Extension:
        return tr("Ext");
    case SortColumn::Size:
        return tr("Size");
    case SortColumn::Modified:
        return tr("Date");
    case SortColumn::Attributes:
        return tr("Attr");
    case SortColumn::Count:
        break;
    }
    return QVariant();
}

void FileListModel::sort(int column, Qt::SortOrder order)
{
    if (column < 0 || column >= static_cast<int>(SortColumn::Count)) {
        return;
    }
    beginResetModel();
    m_sortColumn = static_cast<SortColumn>(column);
    m_sortOrder = order;
    sortFileItems(m_items, m_sortColumn, m_sortOrder);
    endResetModel();
}

SortColumn FileListModel::sortColumn() const
{
    return m_sortColumn;
}

Qt::SortOrder FileListModel::sortOrder() const
{
    return m_sortOrder;
}

bool FileListModel::isParentRow(int row) const
{
    return m_showParent && row == 0;
}

int FileListModel::itemIndex(int row) const
{
    const int i = row - (m_showParent ? 1 : 0);
    return (i >= 0 && i < m_items.size()) ? i : -1;
}

const FileItem *FileListModel::itemAt(int row) const
{
    const int i = itemIndex(row);
    return i < 0 ? nullptr : &m_items.at(i);
}

int FileListModel::rowOfName(const QString &name) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).name == name) {
            return i + (m_showParent ? 1 : 0);
        }
    }
    return -1;
}

void FileListModel::emitRowChanged(int row)
{
    emit dataChanged(index(row, 0), index(row, columnCount() - 1), {Qt::ForegroundRole});
}

void FileListModel::toggleMark(int row)
{
    const FileItem *item = itemAt(row);
    if (item == nullptr) {
        return;
    }
    if (m_marked.contains(item->name)) {
        m_marked.remove(item->name);
    } else {
        m_marked.insert(item->name);
    }
    emitRowChanged(row);
    emit marksChanged();
}

void FileListModel::markByMask(const FileMask &mask, bool mark)
{
    for (const FileItem &item : std::as_const(m_items)) {
        if (item.isDir || !mask.matches(item.name)) {
            continue;
        }
        if (mark) {
            m_marked.insert(item.name);
        } else {
            m_marked.remove(item.name);
        }
    }
    if (rowCount() > 0) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1), {Qt::ForegroundRole});
    }
    emit marksChanged();
}

QStringList FileListModel::markedNames() const
{
    QStringList names;
    for (const FileItem &item : m_items) {
        if (m_marked.contains(item.name)) {
            names.append(item.name);
        }
    }
    return names;
}

int FileListModel::markedCount() const
{
    return static_cast<int>(m_marked.size());
}

qint64 FileListModel::markedSize() const
{
    qint64 total = 0;
    for (const FileItem &item : m_items) {
        if (m_marked.contains(item.name)) {
            total += item.size;
        }
    }
    return total;
}

int FileListModel::fileCount() const
{
    return static_cast<int>(m_items.size());
}

qint64 FileListModel::totalSize() const
{
    qint64 total = 0;
    for (const FileItem &item : m_items) {
        total += item.size;
    }
    return total;
}

} // namespace dcpp
