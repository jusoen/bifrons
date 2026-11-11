#pragma once

#include <QAbstractTableModel>
#include <QIcon>
#include <QSet>

#include "core/filemask.h"
#include "core/filesort.h"

namespace bifrons {

// Optional ".." row plus sorted entries. Marks are tracked by name, apart from the cursor.
class FileListModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit FileListModel(QObject *parent = nullptr);

    void setItems(FileItemList items, bool showParent, bool keepMarks);
    // An inactive panel draws its cursor row pale, so the ".." arrow stays dark there.
    void setPanelActive(bool active);
    // Hiding drops marks on hidden entries.
    void setShowHidden(bool show);
    bool showHidden() const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    SortColumn sortColumn() const;
    Qt::SortOrder sortOrder() const;

    bool isParentRow(int row) const;
    // nullptr for the ".." row or an out-of-range row.
    const FileItem *itemAt(int row) const;
    // -1 when no entry has that name.
    int rowOfName(const QString &name) const;

    void toggleMark(int row);
    // Rows in either order. The ".." row is never marked.
    void markRange(int fromRow, int toRow, bool mark);
    void markAll(bool mark);
    void markByMask(const FileMask &mask, bool mark);
    QStringList markedNames() const;
    int markedCount() const;
    qint64 markedSize() const;
    int fileCount() const;
    qint64 totalSize() const;

signals:
    void marksChanged();

private:
    int itemIndex(int row) const;
    void emitRowChanged(int row);

    FileItemList m_all;
    FileItemList m_items;
    QSet<QString> m_marked;
    bool m_showParent = false;
    bool m_showHidden = false;
    SortColumn m_sortColumn = SortColumn::Name;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
    QIcon m_dirIcon;
    QIcon m_fileIcon;
    // Up arrow for the ".." row, matching the toolbar's Parent folder button.
    QIcon m_parentIcon;
    // Same arrow, drawn dark even on the cursor row.
    QIcon m_parentIconInactive;
    bool m_panelActive = false;
};

} // namespace bifrons
