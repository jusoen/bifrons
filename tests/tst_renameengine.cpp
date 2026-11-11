#include <QtTest>

#include "core/renameengine.h"

using namespace dcpp;

namespace {

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    return f.write(data) == data.size();
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return f.readAll();
}

RenameItem fileItem(const QString &path)
{
    return {path, false, QDateTime(QDate(2024, 3, 9), QTime(14, 5, 7))};
}

} // namespace

class TestRenameEngine : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir *m_tmp = nullptr;
    QString m_dir;

    QString make(const QString &name, const QByteArray &data = "x")
    {
        const QString path = m_dir + QLatin1Char('/') + name;
        return writeFile(path, data) ? path : QString();
    }

private slots:
    void init()
    {
        m_tmp = new QTemporaryDir;
        QVERIFY(m_tmp->isValid());
        m_dir = QDir::cleanPath(m_tmp->path());
    }

    void cleanup()
    {
        delete m_tmp;
        m_tmp = nullptr;
    }

    void masksAndCounter()
    {
        RenameOptions o;
        o.nameMask = QStringLiteral("photo_[C]_[N1-3]");
        o.extMask = QStringLiteral("[E]");
        o.counterStart = 8;
        o.counterWidth = 3;
        const RenamePreview p = previewRenames({fileItem(make("holiday.JPG")), fileItem(make("beach.jpg"))}, o);
        QVERIFY(p.ok());
        QCOMPARE(p.newNames, (QStringList{"photo_008_hol.JPG", "photo_009_bea.jpg"}));
    }

    void dateTimeAndParent()
    {
        RenameOptions o;
        o.nameMask = QStringLiteral("[Y]-[M]-[D]_[h][n][s]_[P]");
        const QString parent = QFileInfo(m_dir).fileName();
        const RenamePreview p = previewRenames({fileItem(make("a.txt"))}, o);
        QVERIFY(p.ok());
        QCOMPARE(p.newNames.first(), QStringLiteral("2024-03-09_140507_%1.txt").arg(parent));
    }

    void rangesAndLiteralBracket()
    {
        RenameOptions o;
        o.nameMask = QStringLiteral("[[[N3-]");
        o.extMask = QString();
        const RenamePreview p = previewRenames({fileItem(make("abcdef.txt"))}, o);
        QVERIFY(p.ok());
        QCOMPARE(p.newNames.first(), QStringLiteral("[cdef"));
    }

    void badMaskReportsError()
    {
        RenameOptions o;
        o.nameMask = QStringLiteral("[Q]");
        QVERIFY(!previewRenames({fileItem(make("a.txt"))}, o).ok());
        o.nameMask = QStringLiteral("[N");
        QVERIFY(!previewRenames({fileItem(make("a.txt"))}, o).ok());
    }

    void findReplacePlainAndRegex()
    {
        RenameOptions o;
        o.find = QStringLiteral("IMG");
        o.replace = QStringLiteral("pic");
        QCOMPARE(previewRenames({fileItem(make("img_01.png"))}, o).newNames.first(), QStringLiteral("pic_01.png"));

        o.regex = true;
        o.find = QStringLiteral("(\\d+)");
        o.replace = QStringLiteral("n\\1");
        QCOMPARE(previewRenames({fileItem(make("img_02.png"))}, o).newNames.first(), QStringLiteral("img_n02.png"));

        o.find = QStringLiteral("(");
        QVERIFY(!previewRenames({fileItem(make("img_03.png"))}, o).ok());
    }

    void caseModes()
    {
        RenameOptions o;
        o.caseMode = RenameOptions::Case::EachWord;
        QCOMPARE(previewRenames({fileItem(make("my FILE-name.TXT"))}, o).newNames.first(),
                 QStringLiteral("My File-Name.Txt"));
        o.caseMode = RenameOptions::Case::Lower;
        QCOMPARE(previewRenames({fileItem(make("ABC.TXT"))}, o).newNames.first(), QStringLiteral("abc.txt"));
    }

    void detectsDuplicatesAndExisting()
    {
        RenameOptions o;
        o.nameMask = QStringLiteral("same");
        const RenamePreview dup = previewRenames({fileItem(make("a.txt")), fileItem(make("b.txt"))}, o);
        QVERIFY(!dup.ok());
        QVERIFY(dup.problems.at(0).isEmpty());
        QVERIFY(!dup.problems.at(1).isEmpty());

        make("taken.txt");
        o.nameMask = QStringLiteral("taken");
        QVERIFY(!previewRenames({fileItem(make("c.txt"))}, o).ok());
    }

    void rejectsInvalidCharacters()
    {
        RenameOptions o;
        o.nameMask = QStringLiteral("a/b");
        QVERIFY(!previewRenames({fileItem(make("a.txt"))}, o).ok());
    }

    void swapNamesAndUndo()
    {
        const QString a = make("a.txt", "AAA");
        const QString b = make("b.txt", "BBB");
        const RenameResult r = applyRenames({fileItem(a), fileItem(b)}, {"b.txt", "a.txt"});
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(readFile(a), QByteArray("BBB"));
        QCOMPARE(readFile(b), QByteArray("AAA"));
        QCOMPARE(QDir(m_dir).entryList(QDir::Files | QDir::Hidden).size(), 2);

        const RenameResult u = undoRenames(r.done);
        QVERIFY2(u.ok, qPrintable(u.error));
        QCOMPARE(readFile(a), QByteArray("AAA"));
        QCOMPARE(readFile(b), QByteArray("BBB"));
    }

    void previewAllowsSwap()
    {
        const QString a = make("a.txt");
        const QString b = make("b.txt");
        RenameOptions o;
        o.find = QStringLiteral("x");
        const RenamePreview p = previewRenames({fileItem(a), fileItem(b)}, o);
        QVERIFY(p.ok());
    }

    void failureRollsBack()
    {
        const QString a = make("a.txt", "AAA");
        const QString b = make("b.txt", "BBB");
        make("c.txt", "CCC");
        // Second target already exists, so phase 2 fails and everything is restored.
        const RenameResult r = applyRenames({fileItem(a), fileItem(b)}, {"z.txt", "c.txt"});
        QVERIFY(!r.ok);
        QCOMPARE(readFile(a), QByteArray("AAA"));
        QCOMPARE(readFile(b), QByteArray("BBB"));
        QCOMPARE(readFile(m_dir + QStringLiteral("/c.txt")), QByteArray("CCC"));
        QVERIFY(!QFileInfo::exists(m_dir + QStringLiteral("/z.txt")));
        QCOMPARE(QDir(m_dir).entryList(QDir::Files | QDir::Hidden).size(), 3);
    }

    void caseOnlyRename()
    {
        const QString a = make("readme.md", "R");
        const RenameResult r = applyRenames({fileItem(a)}, {"README.md"});
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(QDir(m_dir).entryList(QDir::Files).contains(QStringLiteral("README.md")));
    }

    void unchangedNamesAreSkipped()
    {
        const QString a = make("same.txt");
        const RenameResult r = applyRenames({fileItem(a)}, {"same.txt"});
        QVERIFY(r.ok);
        QVERIFY(r.done.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestRenameEngine)
#include "tst_renameengine.moc"
