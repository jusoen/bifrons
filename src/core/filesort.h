#pragma once

#include <Qt>

#include "core/fileitem.h"

namespace dcpp {

enum class SortColumn
{
    Name = 0,
    Extension,
    Size,
    Modified,
    Attributes,
    Count
};

// Directories first, natural case-insensitive name order ("file2" < "file10").
void sortFileItems(FileItemList &items, SortColumn column, Qt::SortOrder order);

} // namespace dcpp
