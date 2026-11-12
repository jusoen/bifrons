#include <QtTest>

#include "core/filemask.h"

using bifrons::FileMask;
using bifrons::QuickFilter;

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

    void quickFilterEmptyMatchesAll()
    {
        const QuickFilter filter(QStringLiteral("  "));
        QVERIFY(filter.isEmpty());
        QVERIFY(filter.matches(QStringLiteral("anything.txt")));
    }

    void quickFilterPlainTextMatchesAnywhere()
    {
        const QuickFilter filter(QStringLiteral("Main"));
        QVERIFY(!filter.isEmpty());
        QVERIFY(filter.matches(QStringLiteral("main.cpp")));
        QVERIFY(filter.matches(QStringLiteral("domain.h")));
        QVERIFY(!filter.matches(QStringLiteral("readme.md")));
    }

    void quickFilterDotIsLiteral()
    {
        const QuickFilter filter(QStringLiteral(".h"));
        QVERIFY(filter.matches(QStringLiteral("a.h")));
        QVERIFY(!filter.matches(QStringLiteral("ah")));
    }

    void quickFilterWildcardsAreAMask()
    {
        const QuickFilter filter(QStringLiteral("*.cpp;*.h"));
        QVERIFY(filter.matches(QStringLiteral("main.CPP")));
        QVERIFY(filter.matches(QStringLiteral("a.h")));
        // A mask matches the whole name, so text after the extension does not match.
        QVERIFY(!filter.matches(QStringLiteral("main.cpp.bak")));
    }

    void quickFilterQuestionMarkIsOneCharacter()
    {
        const QuickFilter filter(QStringLiteral("?"));
        QVERIFY(filter.matches(QStringLiteral("a")));
        QVERIFY(!filter.matches(QStringLiteral("ab")));
    }
};

QTEST_GUILESS_MAIN(TestFileMask)
#include "tst_filemask.moc"
