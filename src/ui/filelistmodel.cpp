#include "ui/filelistmodel.h"

#include <QApplication>
#include <QBrush>
#include <QLocale>
#include <QStyle>

#include <algorithm>

namespace bifrons {

FileListModel::FileListModel(QObject *parent)
    : QAbstractTableModel(parent)
    , m_dirIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon))
    , m_fileIcon(QApplication::style()->standardIcon(QStyle::SP_FileIcon))
    , m_parentIcon(QIcon::fromTheme(QIcon::ThemeIcon::GoUp))
{
    if (m_parentIcon.isNull()) {
        m_parentIcon = QApplication::style()->standardIcon(QStyle::SP_FileDialogToParent);
    }
    for (const QSize &size : {QSize(16, 16), QSize(20, 20), QSize(24, 24), QSize(32, 32)}) {
        const QPixmap normal = m_parentIcon.pixmap(size, QIcon::Normal);
        m_parentIconInactive.addPixmap(normal, QIcon::Normal);
        m_parentIconInactive.addPixmap(normal, QIcon::Selected);
    }
}

void FileListModel::setIconLookup(std::function<QIcon(const FileItem &)> lookup)
{
    m_iconLookup = std::move(lookup);
}

void FileListModel::refreshIcon(const QString &name)
{
    const int row = rowOfName(name);
    if (row >= 0) {
        const QModelIndex cell = index(row, static_cast<int>(SortColumn::Name));
        emit dataChanged(cell, cell, {Qt::DecorationRole});
    }
}

void FileListModel::setDirSize(const QString &name, qint64 size)
{
    m_dirSizes.insert(name, size);
    const int row = rowOfName(name);
    if (row >= 0) {
        const QModelIndex cell = index(row, static_cast<int>(SortColumn::Size));
        emit dataChanged(cell, cell, {Qt::DisplayRole});
    }
}

void FileListModel::clearDirSizes()
{
    if (m_dirSizes.isEmpty()) {
        return;
    }
    m_dirSizes.clear();
    const int column = static_cast<int>(SortColumn::Size);
    if (rowCount() > 0) {
        emit dataChanged(index(0, column), index(rowCount() - 1, column), {Qt::DisplayRole});
    }
}

QStringList FileListModel::dirNames() const
{
    QStringList names;
    for (const FileItem &item : m_all) {
        if (item.isDir) {
            names.append(item.name);
        }
    }
    return names;
}

void FileListModel::setPanelActive(bool active)
{
    if (m_panelActive == active) {
        return;
    }
    m_panelActive = active;
    if (m_showParent) {
        const QModelIndex cell = index(0, static_cast<int>(SortColumn::Name));
        emit dataChanged(cell, cell, {Qt::DecorationRole});
    }
}

void FileListModel::setShowHidden(bool show)
{
    if (show == m_showHidden) {
        return;
    }
    m_showHidden = show;
    setItems(m_all, m_showParent, true);
}

bool FileListModel::showHidden() const
{
    return m_showHidden;
}

void FileListModel::setNameFilter(const QString &text)
{
    if (text == m_filterText) {
        return;
    }
    m_filterText = text;
    m_filter = QuickFilter(text);
    setItems(m_all, m_showParent, true);
}

void FileListModel::setDropRow(int row)
{
    if (row == m_dropRow) {
        return;
    }
    const int old = m_dropRow;
    m_dropRow = row;
    for (const int r : {old, row}) {
        if (r >= 0 && r < rowCount()) {
            emit dataChanged(index(r, 0), index(r, columnCount() - 1), {Qt::BackgroundRole});
        }
    }
}

void FileListModel::clearNameFilterQuietly()
{
    m_filterText.clear();
    m_filter = QuickFilter();
}

bool FileListModel::isFiltered() const
{
    return !m_filter.isEmpty();
}

void FileListModel::setItems(FileItemList items, bool showParent, bool keepMarks)
{
    beginResetModel();
    m_dropRow = -1;
    m_all = std::move(items);
    m_items.clear();
    for (const FileItem &item : std::as_const(m_all)) {
        if ((m_showHidden || !item.isHidden) && m_filter.matches(item.name)) {
            m_items.append(item);
        }
    }
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
        // A different folder: its subfolders' sizes are not known yet.
        m_dirSizes.clear();
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

    if (role == Qt::BackgroundRole && index.row() == m_dropRow) {
        constexpr int DropTintAlpha = 90;
        QColor tint = QApplication::palette().color(QPalette::Highlight);
        tint.setAlpha(DropTintAlpha);
        return QBrush(tint);
    }

    if (isParentRow(index.row())) {
        if (column == SortColumn::Name && role == Qt::DisplayRole) {
            return QStringLiteral("..");
        }
        if (column == SortColumn::Name && role == Qt::DecorationRole) {
            return m_panelActive ? m_parentIcon : m_parentIconInactive;
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
            if (item->isDir) {
                const auto size = m_dirSizes.constFind(item->name);
                return size == m_dirSizes.constEnd() ? QStringLiteral("<DIR>") : QLocale().toString(size.value());
            }
            return QLocale().toString(item->size);
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
            if (m_iconLookup) {
                return m_iconLookup(*item);
            }
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

void FileListModel::markRange(int fromRow, int toRow, bool mark)
{
    if (rowCount() == 0) {
        return;
    }
    const int first = std::clamp(std::min(fromRow, toRow), 0, rowCount() - 1);
    const int last = std::clamp(std::max(fromRow, toRow), 0, rowCount() - 1);
    for (int row = first; row <= last; ++row) {
        const FileItem *item = itemAt(row);
        if (item == nullptr) {
            continue;
        }
        if (mark) {
            m_marked.insert(item->name);
        } else {
            m_marked.remove(item->name);
        }
    }
    emit dataChanged(index(first, 0), index(last, columnCount() - 1), {Qt::ForegroundRole});
    emit marksChanged();
}

void FileListModel::markAll(bool mark)
{
    markRange(0, rowCount() - 1, mark);
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

} // namespace bifrons
