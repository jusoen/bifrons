#include <QtTest>

#include "core/favourites.h"

using namespace bifrons;

class TestFavourites : public QObject
{
    Q_OBJECT

private slots:
    void addSkipsDuplicates()
    {
        QTemporaryDir tmp;
        FavouriteList list;
        QVERIFY(addFavourite(list, tmp.path()));
        QVERIFY(!addFavourite(list, tmp.path() + QStringLiteral("/")));
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().name, QDir(tmp.path()).dirName());
#if defined(Q_OS_WIN)
        QVERIFY(!addFavourite(list, tmp.path().toUpper()));
#endif
        QVERIFY(!addFavourite(list, QString()));
    }

    void saveAndLoadRoundTrip()
    {
        QTemporaryDir tmp;
        const QString ini = tmp.filePath(QStringLiteral("fav.ini"));
        const FavouriteList original{{QStringLiteral("Home & Away"), QStringLiteral("C:/a")},
                                     {QStringLiteral("Projects"), QStringLiteral("D:/b")}};
        {
            QSettings s(ini, QSettings::IniFormat);
            saveFavourites(s, original);
        }
        QSettings s(ini, QSettings::IniFormat);
        const FavouriteList loaded = loadFavourites(s);
        QCOMPARE(loaded.size(), 2);
        QCOMPARE(loaded.at(0).name, QStringLiteral("Home & Away"));
        QCOMPARE(loaded.at(1).path, QStringLiteral("D:/b"));
    }

    void saveReplacesOldEntries()
    {
        QTemporaryDir tmp;
        const QString ini = tmp.filePath(QStringLiteral("fav.ini"));
        {
            QSettings s(ini, QSettings::IniFormat);
            saveFavourites(s, {{QStringLiteral("a"), QStringLiteral("C:/a")}, {QStringLiteral("b"), QStringLiteral("C:/b")}});
            saveFavourites(s, {{QStringLiteral("c"), QStringLiteral("C:/c")}});
        }
        QSettings s(ini, QSettings::IniFormat);
        const FavouriteList loaded = loadFavourites(s);
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded.first().name, QStringLiteral("c"));
    }
};

QTEST_GUILESS_MAIN(TestFavourites)
#include "tst_favourites.moc"
