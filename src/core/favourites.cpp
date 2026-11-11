#include "core/favourites.h"

#include <QDir>
#include <QSettings>

#include "core/localfilesource.h"

#include <algorithm>

namespace bifrons {

namespace {

constexpr int MaxFavourites = 500;

Qt::CaseSensitivity pathCase()
{
#if defined(Q_OS_WIN)
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QString defaultName(const QString &path)
{
    const QString name = QDir(path).dirName();
    return name.isEmpty() ? QDir::toNativeSeparators(path) : name;
}

} // namespace

FavouriteList loadFavourites(QSettings &s)
{
    FavouriteList list;
    const int n = s.beginReadArray(QStringLiteral("favourites"));
    for (int i = 0; i < std::min(n, MaxFavourites); ++i) {
        s.setArrayIndex(i);
        const QString path = s.value(QStringLiteral("path")).toString();
        if (path.isEmpty()) {
            continue;
        }
        const QString name = s.value(QStringLiteral("name")).toString();
        list.append({name.isEmpty() ? defaultName(path) : name, path});
    }
    s.endArray();
    return list;
}

void saveFavourites(QSettings &s, const FavouriteList &list)
{
    s.remove(QStringLiteral("favourites"));
    s.beginWriteArray(QStringLiteral("favourites"), static_cast<int>(list.size()));
    for (int i = 0; i < list.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("name"), list.at(i).name);
        s.setValue(QStringLiteral("path"), list.at(i).path);
    }
    s.endArray();
}

int indexOfFavourite(const FavouriteList &list, const QString &path)
{
    const QString wanted = QDir::cleanPath(path);
    for (int i = 0; i < list.size(); ++i) {
        if (QDir::cleanPath(list.at(i).path).compare(wanted, pathCase()) == 0) {
            return i;
        }
    }
    return -1;
}

bool addFavourite(FavouriteList &list, const QString &path, const QString &name)
{
    if (path.isEmpty() || list.size() >= MaxFavourites || indexOfFavourite(list, path) >= 0) {
        return false;
    }
    const QString clean = LocalFileSource::normalise(path);
    list.append({name.isEmpty() ? defaultName(clean) : name, clean});
    return true;
}

} // namespace bifrons
