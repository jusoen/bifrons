#pragma once

#include <QList>
#include <QString>

class QSettings;

namespace dcpp {

struct Favourite
{
    QString name;
    QString path;
};

using FavouriteList = QList<Favourite>;

// Reads and writes the "favourites" array in s. Entries with an empty path are dropped.
FavouriteList loadFavourites(QSettings &s);
void saveFavourites(QSettings &s, const FavouriteList &list);

// Appends path unless it is already listed. Name defaults to the folder name.
bool addFavourite(FavouriteList &list, const QString &path, const QString &name = QString());
int indexOfFavourite(const FavouriteList &list, const QString &path);

} // namespace dcpp
