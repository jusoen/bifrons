#include <QtTest>

#include "core/appinfo.h"

// Smoke test: core links and ctest runs.
class TestAppInfo : public QObject
{
    Q_OBJECT

private slots:
    void versionMatchesProject()
    {
        QCOMPARE(dcpp::appVersion(), QStringLiteral(DCPP_VERSION));
        QVERIFY(!dcpp::appVersion().isEmpty());
    }

    void displayName()
    {
        QCOMPARE(dcpp::appDisplayName(), QStringLiteral("Bifrons"));
    }
};

QTEST_GUILESS_MAIN(TestAppInfo)
#include "tst_appinfo.moc"
