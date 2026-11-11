#pragma once

#include <QDialog>

#include "core/favourites.h"

class QTableWidget;

namespace bifrons {

// Rename, reorder, edit and remove favourite folders. Changes apply only on OK.
class FavouritesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FavouritesDialog(const FavouriteList &list, QWidget *parent = nullptr);

    FavouriteList favourites() const;

private:
    void moveSelected(int delta);
    void addRow(const Favourite &fav, int row);

    QTableWidget *m_table = nullptr;
};

} // namespace bifrons
