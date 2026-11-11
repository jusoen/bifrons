#include <QtTest>

#include "core/appinfo.h"

// Smoke test: core links and ctest runs.
class TestAppInfo : public QObject
{
    Q_OBJECT

private slots:
    void versionMatchesProject()
    {
        QCOMPARE(bifrons::appVersion(), QStringLiteral(BIFRONS_VERSION));
        QVERIFY(!bifrons::appVersion().isEmpty());
    }

    void displayName()
    {
        QCOMPARE(bifrons::appDisplayName(), QStringLiteral("Bifrons"));
    }
};

QTEST_GUILESS_MAIN(TestAppInfo)
#include "tst_appinfo.moc"
