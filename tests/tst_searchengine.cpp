#include <QtTest>

#include "core/searchengine.h"
#include "core/searchresultfilesource.h"

using namespace bifrons;

namespace {

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    return f.write(data) == data.size();
}

QStringList relativeNames(const QString &base, QStringList paths)
{
    const QDir d(base);
    for (QString &p : paths) {
        p = d.relativeFilePath(p);
    }
    paths.sort();
    return paths;
}

QStringList runSearch(const SearchCriteria &c, SearchSummary *summary = nullptr)
{
    SearchEngine engine(c);
    QStringList all;
    QObject::connect(&engine, &SearchEngine::found, &engine, [&all](const QStringList &p) { all.append(p); });
    engine.run();
    if (summary != nullptr) {
        *summary = engine.summary();
    }
    return relativeNames(c.startDir, all);
}

} // namespace

class TestSearchEngine : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir *m_tmp = nullptr;
    QString m_root;

private slots:
    void init()
    {
        m_tmp = new QTemporaryDir;
        QVERIFY(m_tmp->isValid());
        m_root = m_tmp->path();
        QVERIFY(QDir().mkpath(m_root + QStringLiteral("/sub/deep")));
        QVERIFY(writeFile(m_root + QStringLiteral("/a.txt"), "Hello World"));
        QVERIFY(writeFile(m_root + QStringLiteral("/b.cpp"), "int main() {}"));
        QVERIFY(writeFile(m_root + QStringLiteral("/sub/c.txt"), "nothing here"));
        QVERIFY(writeFile(m_root + QStringLiteral("/sub/deep/d.txt"), "hello again"));
    }

    void cleanup()
    {
        delete m_tmp;
        m_tmp = nullptr;
    }

    void byMaskRecursive()
    {
        SearchCriteria c;
        c.startDir = m_root;
        c.nameMask = QStringLiteral("*.txt");
        QCOMPARE(runSearch(c), (QStringList{"a.txt", "sub/c.txt", "sub/deep/d.txt"}));
    }

    void maskAlsoMatchesFolders()
    {
        SearchCriteria c;
        c.startDir = m_root;
        c.nameMask = QStringLiteral("de*");
        QCOMPARE(runSearch(c), (QStringList{"sub/deep"}));
    }

    void contentCaseInsensitive()
    {
        SearchCriteria c;
        c.startDir = m_root;
        c.text = QStringLiteral("HELLO");
        QCOMPARE(runSearch(c), (QStringList{"a.txt", "sub/deep/d.txt"}));
    }

    void contentCaseSensitive()
    {
        SearchCriteria c;
        c.startDir = m_root;
        c.text = QStringLiteral("Hello");
        c.textCase = Qt::CaseSensitive;
        QCOMPARE(runSearch(c), (QStringList{"a.txt"}));
    }

    void contentAcrossChunkBoundary()
    {
        QByteArray data(1024 * 1024 - 3, 'x');
        data += "NEEDLE";
        data += QByteArray(100, 'y');
        QVERIFY(writeFile(m_root + QStringLiteral("/big.bin"), data));
        QString error;
        QVERIFY(fileContainsText(m_root + QStringLiteral("/big.bin"), QStringLiteral("needle"), Qt::CaseInsensitive,
                                 QStringConverter::Utf8, nullptr, &error));
        QVERIFY(error.isEmpty());
    }

    void contentUtf16()
    {
        const QString text = QStringLiteral("Grüße aus Bonn");
        QByteArray data;
        for (const QChar ch : text) {
            data.append(static_cast<char>(ch.unicode() & 0xFF));
            data.append(static_cast<char>(ch.unicode() >> 8));
        }
        QVERIFY(writeFile(m_root + QStringLiteral("/u16.txt"), data));
        const QString path = m_root + QStringLiteral("/u16.txt");
        QVERIFY(fileContainsText(path, QStringLiteral("grüße AUS"), Qt::CaseInsensitive, QStringConverter::Utf16LE,
                                 nullptr, nullptr));
        QVERIFY(!fileContainsText(path, QStringLiteral("aus"), Qt::CaseInsensitive, QStringConverter::Utf8, nullptr,
                                  nullptr));
    }

    void sizeFilterExcludesFolders()
    {
        SearchCriteria c;
        c.startDir = m_root;
        c.minSize = 12;
        QCOMPARE(runSearch(c), (QStringList{"b.cpp", "sub/c.txt"}));
    }

    void dateFilter()
    {
        const QDateTime old = QDateTime::currentDateTime().addYears(-2);
        {
            QFile f(m_root + QStringLiteral("/a.txt"));
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(old, QFileDevice::FileModificationTime));
        }
        SearchCriteria c;
        c.startDir = m_root;
        c.nameMask = QStringLiteral("*.txt");
        c.modifiedBefore = QDateTime::currentDateTime().addYears(-1);
        QCOMPARE(runSearch(c), (QStringList{"a.txt"}));
    }

    void missingStartDirReportsError()
    {
        SearchCriteria c;
        c.startDir = m_root + QStringLiteral("/nope");
        SearchSummary s;
        QVERIFY(runSearch(c, &s).isEmpty());
        QCOMPARE(s.errors.size(), 1);
    }

    void cancelStopsEarly()
    {
        SearchCriteria c;
        c.startDir = m_root;
        SearchEngine engine(c);
        engine.cancel();
        engine.run();
        QVERIFY(engine.summary().cancelled);
        QCOMPARE(engine.summary().found, 0);
    }

    void resultSourceListsAndDropsMissing()
    {
        const QString a = m_root + QStringLiteral("/a.txt");
        const QString d = m_root + QStringLiteral("/sub/deep/d.txt");
        const SearchResultFileSource fs(m_root, {a, d});
        ListResult r = fs.list(SearchResultFileSource::rootPath());
        QVERIFY(r.ok);
        QCOMPARE(r.items.size(), 2);
        QCOMPARE(r.items.at(1).name, QStringLiteral("sub/deep/d.txt"));
        QCOMPARE(QDir::cleanPath(fs.childPath(SearchResultFileSource::rootPath(), QStringLiteral("sub/deep/d.txt"))),
                 QDir::cleanPath(QFileInfo(d).absoluteFilePath()));
        QVERIFY(fs.parentPath(SearchResultFileSource::rootPath()).isEmpty());

        QVERIFY(QFile::remove(a));
        r = fs.list(SearchResultFileSource::rootPath());
        QCOMPARE(r.items.size(), 1);
        QVERIFY(!fs.makePath(m_root + QStringLiteral("/x"), nullptr));
    }
};

QTEST_GUILESS_MAIN(TestSearchEngine)
#include "tst_searchengine.moc"
