#include "core/filesort.h"

#include <QCollator>

#include <algorithm>

namespace dcpp {

void sortFileItems(FileItemList &items, SortColumn column, Qt::SortOrder order)
{
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);

    const bool descending = order == Qt::DescendingOrder;

    auto byName = [&](const FileItem &a, const FileItem &b) {
        return collator.compare(a.name, b.name);
    };

    // Returns <0, 0 or >0 for the chosen column, before applying the sort order.
    auto byColumn = [&](const FileItem &a, const FileItem &b) -> int {
        switch (column) {
        case SortColumn::Extension: {
            const int c = collator.compare(a.extension(), b.extension());
            return c != 0 ? c : byName(a, b);
        }
        case SortColumn::Size:
            if (a.size != b.size) {
                return a.size < b.size ? -1 : 1;
            }
            return byName(a, b);
        case SortColumn::Modified:
            if (a.modified != b.modified) {
                return a.modified < b.modified ? -1 : 1;
            }
            return byName(a, b);
        case SortColumn::Attributes: {
            const int c = QString::compare(a.attributes, b.attributes);
            return c != 0 ? c : byName(a, b);
        }
        case SortColumn::Name:
        case SortColumn::Count:
            break;
        }
        return byName(a, b);
    };

    std::stable_sort(items.begin(), items.end(), [&](const FileItem &a, const FileItem &b) {
        if (a.isDir != b.isDir) {
            return a.isDir;
        }
        int c = 0;
        if (a.isDir && column != SortColumn::Modified) {
            c = byName(a, b);
        } else {
            c = byColumn(a, b);
        }
        return descending ? c > 0 : c < 0;
    });
}

} // namespace dcpp
