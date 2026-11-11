#include <QtTest>

#include "core/usertools.h"

using namespace bifrons;

namespace {

ToolContext sampleContext()
{
    ToolContext c;
    c.currentDir = QStringLiteral("C:/Work/My Folder");
    c.currentPath = QStringLiteral("C:/Work/My Folder/report 1.txt");
    c.selectedPaths = {QStringLiteral("C:/Work/My Folder/a.txt"), QStringLiteral("C:/Work/My Folder/b c.txt")};
    c.targetDir = QStringLiteral("D:/Backup");
    return c;
}

QString native(const QString &path)
{
    return QDir::toNativeSeparators(path);
}

} // namespace

class TestUserTools : public QObject
{
    Q_OBJECT

private slots:
    void expandsSinglePlaceholders()
    {
        const QStringList args = expandToolArguments(QStringLiteral("%d %p %n %t"), sampleContext());
        QCOMPARE(args, QStringList({native(QStringLiteral("C:/Work/My Folder")),
                                    native(QStringLiteral("C:/Work/My Folder/report 1.txt")),
                                    QStringLiteral("report 1.txt"), native(QStringLiteral("D:/Backup"))}));
    }

    void bareSelectionBecomesOneArgumentPerEntry()
    {
        const QStringList args = expandToolArguments(QStringLiteral("--files %s --end"), sampleContext());
        QCOMPARE(args, QStringList({QStringLiteral("--files"), native(QStringLiteral("C:/Work/My Folder/a.txt")),
                                    native(QStringLiteral("C:/Work/My Folder/b c.txt")), QStringLiteral("--end")}));
    }

    void embeddedSelectionIsQuotedAndJoined()
    {
        const QStringList args = expandToolArguments(QStringLiteral("--files=%s"), sampleContext());
        QCOMPARE(args.size(), 1);
        QCOMPARE(args.first(), QStringLiteral("--files=\"%1\" \"%2\"")
                                   .arg(native(QStringLiteral("C:/Work/My Folder/a.txt")),
                                        native(QStringLiteral("C:/Work/My Folder/b c.txt"))));
    }

    void quotedTemplateTextStaysOneArgument()
    {
        const QStringList args = expandToolArguments(QStringLiteral("\"out dir=%t\" -x"), sampleContext());
        QCOMPARE(args, QStringList({QStringLiteral("out dir=") + native(QStringLiteral("D:/Backup")),
                                    QStringLiteral("-x")}));
    }

    void percentEscapesAndUnknownCodes()
    {
        const QStringList args = expandToolArguments(QStringLiteral("100%% %q %"), sampleContext());
        QCOMPARE(args, QStringList({QStringLiteral("100%"), QStringLiteral("%q"), QStringLiteral("%")}));
    }

    void emptyContextGivesEmptyValues()
    {
        const QStringList args = expandToolArguments(QStringLiteral("[%p] %s"), ToolContext());
        QCOMPARE(args, QStringList({QStringLiteral("[]")}));
    }

    void savesAndLoads()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.filePath(QStringLiteral("tools.ini"));
        const UserToolList tools = {{QStringLiteral("Edit"), QStringLiteral("notepad.exe"), QStringLiteral("%p"),
                                     QString()},
                                    {QStringLiteral("Diff"), QStringLiteral("C:/Tools/diff.exe"),
                                     QStringLiteral("%s"), QStringLiteral("C:/Tools/diff.ico")}};
        {
            QSettings s(file, QSettings::IniFormat);
            saveUserTools(s, tools);
        }
        QSettings s(file, QSettings::IniFormat);
        const UserToolList loaded = loadUserTools(s);
        QCOMPARE(loaded.size(), 2);
        QCOMPARE(loaded.at(1).name, QStringLiteral("Diff"));
        QCOMPARE(loaded.at(1).program, QStringLiteral("C:/Tools/diff.exe"));
        QCOMPARE(loaded.at(1).arguments, QStringLiteral("%s"));
        QCOMPARE(loaded.at(1).icon, QStringLiteral("C:/Tools/diff.ico"));
    }
};

QTEST_GUILESS_MAIN(TestUserTools)
#include "tst_usertools.moc"
