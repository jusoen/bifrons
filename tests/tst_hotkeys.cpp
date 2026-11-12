#include <QtTest>

#include "core/hotkeys.h"

using namespace bifrons;

class TestHotkeys : public QObject
{
    Q_OBJECT

private:
    static HotkeyMap defaults()
    {
        return {{QStringLiteral("cm_Copy"), {QStringLiteral("F5")}},
                {QStringLiteral("cm_Delete"), {QStringLiteral("F8"), QStringLiteral("Del")}},
                {QStringLiteral("cm_QuickFilter"), {QStringLiteral("Ctrl+F")}}};
    }

private slots:
    void onlyChangesAreSaved()
    {
        QTemporaryDir dir;
        QSettings s(dir.filePath(QStringLiteral("h.ini")), QSettings::IniFormat);
        HotkeyMap effective = defaults();
        effective[QStringLiteral("cm_Copy")] = {QStringLiteral("Ctrl+C")};
        saveHotkeys(s, effective, defaults());
        s.beginGroup(QStringLiteral("hotkeys"));
        QCOMPARE(s.childKeys(), QStringList{QStringLiteral("cm_Copy")});
        s.endGroup();
    }

    void roundTripKeepsListsAndNoKey()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("h.ini"));
        HotkeyMap effective = defaults();
        effective[QStringLiteral("cm_Delete")] = {QStringLiteral("Del"), QStringLiteral("Shift+Del")};
        effective[QStringLiteral("cm_QuickFilter")] = {};
        {
            QSettings s(path, QSettings::IniFormat);
            saveHotkeys(s, effective, defaults());
        }
        QSettings s(path, QSettings::IniFormat);
        const HotkeyMap loaded = loadHotkeys(s);
        QCOMPARE(loaded.size(), 2);
        QCOMPARE(loaded.value(QStringLiteral("cm_Delete")),
                 (QStringList{QStringLiteral("Del"), QStringLiteral("Shift+Del")}));
        QVERIFY(loaded.contains(QStringLiteral("cm_QuickFilter")));
        QVERIFY(loaded.value(QStringLiteral("cm_QuickFilter")).isEmpty());
        QCOMPARE(mergeHotkeys(defaults(), loaded), effective);
    }

    void saveClearsEarlierOverrides()
    {
        QTemporaryDir dir;
        QSettings s(dir.filePath(QStringLiteral("h.ini")), QSettings::IniFormat);
        HotkeyMap effective = defaults();
        effective[QStringLiteral("cm_Copy")] = {QStringLiteral("Ctrl+C")};
        saveHotkeys(s, effective, defaults());
        saveHotkeys(s, defaults(), defaults());
        QVERIFY(loadHotkeys(s).isEmpty());
    }

    void mergeIgnoresUnknownCommands()
    {
        const HotkeyMap merged =
            mergeHotkeys(defaults(), {{QStringLiteral("cm_Gone"), {QStringLiteral("F2")}}});
        QCOMPARE(merged, defaults());
    }

    void clashesListEveryUser()
    {
        HotkeyMap map = defaults();
        map[QStringLiteral("cm_Copy")] = {QStringLiteral("F8")};
        const QHash<QString, QStringList> clashes = findClashes(map);
        QCOMPARE(clashes.size(), 1);
        QCOMPARE(clashes.value(QStringLiteral("F8")),
                 (QStringList{QStringLiteral("cm_Copy"), QStringLiteral("cm_Delete")}));
        QVERIFY(findClashes(defaults()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestHotkeys)
#include "tst_hotkeys.moc"
