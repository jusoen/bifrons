#include <QtTest>

#include "core/filemask.h"

using bifrons::FileMask;

class TestFileMask : public QObject
{
    Q_OBJECT

private slots:
    void starDotStarMatchesEverything()
    {
        const FileMask mask(QStringLiteral("*.*"));
        QVERIFY(mask.isValid());
        QVERIFY(mask.matches(QStringLiteral("readme")));
        QVERIFY(mask.matches(QStringLiteral("a.txt")));
    }

    void caseInsensitive()
    {
        const FileMask mask(QStringLiteral("*.CPP"));
        QVERIFY(mask.matches(QStringLiteral("main.cpp")));
        QVERIFY(!mask.matches(QStringLiteral("main.h")));
    }

    void multipleMasks()
    {
        const FileMask mask(QStringLiteral("*.cpp; *.h ;"));
        QVERIFY(mask.matches(QStringLiteral("a.h")));
        QVERIFY(mask.matches(QStringLiteral("b.cpp")));
        QVERIFY(!mask.matches(QStringLiteral("c.txt")));
    }

    void questionMark()
    {
        const FileMask mask(QStringLiteral("file?.log"));
        QVERIFY(mask.matches(QStringLiteral("file1.log")));
        QVERIFY(!mask.matches(QStringLiteral("file12.log")));
    }

    void emptyIsInvalid()
    {
        QVERIFY(!FileMask(QStringLiteral(" ; ")).isValid());
        QVERIFY(!FileMask(QStringLiteral(" ; ")).matches(QStringLiteral("x")));
    }
};

QTEST_GUILESS_MAIN(TestFileMask)
#include "tst_filemask.moc"
