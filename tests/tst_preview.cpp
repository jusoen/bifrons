#include <QtTest>

#include "core/codehighlight.h"
#include "core/markdownprep.h"
#include "core/textsniff.h"

using namespace bifrons;

namespace {

QStringList tokensOf(const QString &code, const QString &lang, TokenKind kind)
{
    QStringList out;
    for (const CodeToken &t : highlightCode(code, lang)) {
        if (t.kind == kind) {
            out.append(code.mid(t.start, t.length));
        }
    }
    return out;
}

} // namespace

class TestPreview : public QObject
{
    Q_OBJECT

private slots:
    void cppTokens()
    {
        const QString code = QStringLiteral("#include <x>\nint main() { return 42; } // done\n/* block */ \"s\\\"q\"");
        QCOMPARE(tokensOf(code, QStringLiteral("cpp"), TokenKind::Preprocessor), (QStringList{"#include <x>"}));
        QCOMPARE(tokensOf(code, QStringLiteral("c++"), TokenKind::Keyword), (QStringList{"return"}));
        QCOMPARE(tokensOf(code, QStringLiteral("cpp"), TokenKind::Type), (QStringList{"int"}));
        QCOMPARE(tokensOf(code, QStringLiteral("cpp"), TokenKind::Number), (QStringList{"42"}));
        QCOMPARE(tokensOf(code, QStringLiteral("cpp"), TokenKind::Comment), (QStringList{"// done", "/* block */"}));
        QCOMPARE(tokensOf(code, QStringLiteral("cpp"), TokenKind::String), (QStringList{"\"s\\\"q\""}));
    }

    void pythonTripleQuotesAndComments()
    {
        const QString code = QStringLiteral("def f():\n    \"\"\"doc\nstring\"\"\"\n    return None  # c");
        QCOMPARE(tokensOf(code, QStringLiteral("py"), TokenKind::String), (QStringList{"\"\"\"doc\nstring\"\"\""}));
        QCOMPARE(tokensOf(code, QStringLiteral("python"), TokenKind::Keyword), (QStringList{"def", "return", "None"}));
        QCOMPARE(tokensOf(code, QStringLiteral("python"), TokenKind::Comment), (QStringList{"# c"}));
    }

    void caseInsensitiveLanguages()
    {
        QCOMPARE(tokensOf(QStringLiteral("BEGIN x := 1 END;"), QStringLiteral("pascal"), TokenKind::Keyword),
                 (QStringList{"BEGIN", "END"}));
        QCOMPARE(tokensOf(QStringLiteral("SELECT a FROM t -- note"), QStringLiteral("SQL"), TokenKind::Comment),
                 (QStringList{"-- note"}));
    }

    void markup()
    {
        const QString code = QStringLiteral("<a href=\"x\">t</a><!-- c -->");
        QCOMPARE(tokensOf(code, QStringLiteral("html"), TokenKind::Attribute), (QStringList{"href"}));
        QCOMPARE(tokensOf(code, QStringLiteral("html"), TokenKind::String), (QStringList{"\"x\""}));
        QCOMPARE(tokensOf(code, QStringLiteral("html"), TokenKind::Comment), (QStringList{"<!-- c -->"}));
    }

    void unknownLanguageIsPlain()
    {
        QVERIFY(highlightCode(QStringLiteral("int x;"), QStringLiteral("klingon")).isEmpty());
        QVERIFY(highlightCode(QStringLiteral("int x;"), QString()).isEmpty());
        QVERIFY(!isKnownLanguage(QStringLiteral("klingon")));
        QVERIFY(isKnownLanguage(QStringLiteral("TypeScript")));
    }

    void unterminatedConstructsEnd()
    {
        QCOMPARE(highlightCode(QStringLiteral("/* open"), QStringLiteral("cpp")).size(), 1);
        QCOMPARE(highlightCode(QStringLiteral("\"open"), QStringLiteral("cpp")).size(), 1);
        QCOMPARE(highlightCode(QStringLiteral("<a href=\"open"), QStringLiteral("xml")).size(), 3);
    }

    void textDetection()
    {
        QVERIFY(looksLikeText("hello\r\nworld\t!"));
        QVERIFY(looksLikeText(QByteArray()));
        QVERIFY(!looksLikeText(QByteArray("MZ\x90\x00\x03", 5)));
        QVERIFY(looksLikeText(QByteArray("\xFF\xFEh\x00i\x00", 6)));
    }

    void decoding()
    {
        QString enc;
        QCOMPARE(decodeText(QByteArray("\xEF\xBB\xBFGr\xC3\xBC\xC3\x9F"), &enc), QStringLiteral("Grüß"));
        QCOMPARE(decodeText(QByteArray("Gr\xC3\xBC\xC3\x9F"), &enc), QStringLiteral("Grüß"));
        QCOMPARE(enc, QStringLiteral("UTF-8"));
        QCOMPARE(decodeText(QByteArray("\xFF\xFEh\x00i\x00", 6), &enc), QStringLiteral("hi"));
        (void)decodeText(QByteArray("caf\xE9"), &enc);
        QCOMPARE(enc, QStringLiteral("System"));
        // A character cut off by a truncated read is still UTF-8.
        (void)decodeText(QByteArray("Gr\xC3\xBC\xC3"), &enc, true);
        QCOMPARE(enc, QStringLiteral("UTF-8"));
        (void)decodeText(QByteArray("Gr\xC3\xBC\xC3"), &enc, false);
        QCOMPARE(enc, QStringLiteral("System"));
    }

    void labelBreaks()
    {
        const QString in = QStringLiteral("Intro line\n**Status:** done\n**Owner**: me\nwrapped\ntext\n\n```\nx\n**Not:** here\n```");
        const QString out = applyLabelBreaks(in);
        const QStringList lines = out.split(QLatin1Char('\n'));
        QCOMPARE(lines.at(0), QStringLiteral("Intro line  "));
        QCOMPARE(lines.at(1), QStringLiteral("**Status:** done  "));
        QCOMPARE(lines.at(2), QStringLiteral("**Owner**: me"));
        QCOMPARE(lines.at(3), QStringLiteral("wrapped"));
        QCOMPARE(lines.at(7), QStringLiteral("x"));
    }

    void slugs()
    {
        QCOMPARE(headingSlug(QStringLiteral("Markdown Preview Test")), QStringLiteral("markdown-preview-test"));
        QCOMPARE(headingSlug(QStringLiteral("Code block!")), QStringLiteral("code-block"));
        QCOMPARE(headingSlug(QStringLiteral("  C++ & Qt_6 ")), QStringLiteral("c--qt_6"));
    }
};

QTEST_GUILESS_MAIN(TestPreview)
#include "tst_preview.moc"
