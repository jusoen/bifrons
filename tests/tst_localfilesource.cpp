#include <QtTest>

#include "core/localfilesource.h"

using namespace bifrons;

class TestLocalFileSource : public QObject
{
    Q_OBJECT

private slots:
#if defined(Q_OS_WIN)
    void uncPathsWalkUpToServer()
    {
        const LocalFileSource fs;
        QCOMPARE(LocalFileSource::normalise(QStringLiteral("\\\\srv\\share\\dir\\")), QStringLiteral("//srv/share/dir"));
        QVERIFY(LocalFileSource::isUncServer(QStringLiteral("//srv")));
        QVERIFY(LocalFileSource::isUncServer(QStringLiteral("\\\\srv\\")));
        QVERIFY(!LocalFileSource::isUncServer(QStringLiteral("//srv/share")));
        QVERIFY(!LocalFileSource::isUncServer(QStringLiteral("C:/srv")));
        QVERIFY(!LocalFileSource::isUncServer(QStringLiteral("//")));
        QCOMPARE(fs.parentPath(QStringLiteral("//srv/share/dir")), QStringLiteral("//srv/share"));
        QCOMPARE(fs.parentPath(QStringLiteral("//srv/share")), QStringLiteral("//srv"));
        QVERIFY(fs.parentPath(QStringLiteral("//srv")).isEmpty());
        QCOMPARE(fs.childPath(QStringLiteral("//srv"), QStringLiteral("share")), QStringLiteral("//srv/share"));
    }

    void listsServerShares()
    {
        const LocalFileSource fs;
        const ListResult r = fs.list(QStringLiteral("//localhost"));
        if (!r.ok) {
            QSKIP("No shares visible on localhost");
        }
        for (const FileItem &item : r.items) {
            QVERIFY(item.isDir);
            QVERIFY(!item.name.isEmpty());
            // IPC$ is not a disk share and must be filtered out.
            QVERIFY(item.name.compare(QLatin1String("IPC$"), Qt::CaseInsensitive) != 0);
            if (item.name.endsWith(QLatin1Char('$'))) {
                QVERIFY(item.isHidden);
            }
        }
    }
#endif

    void listsEntries()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("sub")));
        QFile f(tmp.filePath(QStringLiteral("a.txt")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write("hello"), qint64(5));
        f.close();

        const LocalFileSource fs;
        const ListResult r = fs.list(tmp.path());
        QVERIFY(r.ok);
        QCOMPARE(r.items.size(), 2);
        for (const FileItem &item : r.items) {
            if (item.name == QLatin1String("sub")) {
                QVERIFY(item.isDir);
                QCOMPARE(item.size, qint64(0));
            } else {
                QCOMPARE(item.name, QStringLiteral("a.txt"));
                QCOMPARE(item.size, qint64(5));
                QVERIFY(!item.isDir);
            }
        }
    }

    void missingDirectoryFails()
    {
        QTemporaryDir tmp;
        const LocalFileSource fs;
        const ListResult r = fs.list(tmp.filePath(QStringLiteral("nope")));
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
    }

    void parentOfRootIsEmpty()
    {
        const LocalFileSource fs;
        const QString root = LocalFileSource::normalise(QDir::rootPath());
        QVERIFY(fs.parentPath(root).isEmpty());
        QTemporaryDir tmp;
        const QString child = LocalFileSource::normalise(tmp.filePath(QStringLiteral("x")));
        QCOMPARE(fs.parentPath(child), LocalFileSource::normalise(tmp.path()));
    }

    void makePathNested()
    {
        QTemporaryDir tmp;
        const LocalFileSource fs;
        const QString deep = tmp.filePath(QStringLiteral("a/b/c"));
        QString error;
        QVERIFY(fs.makePath(deep, &error));
        QVERIFY(QFileInfo(deep).isDir());
    }

    void nearestExistingDirWalksUp()
    {
        QTemporaryDir tmp;
        const QString missing = tmp.filePath(QStringLiteral("gone/deeper"));
        QCOMPARE(LocalFileSource::nearestExistingDir(missing), LocalFileSource::normalise(tmp.path()));
        QCOMPARE(LocalFileSource::nearestExistingDir(QString()), LocalFileSource::normalise(QDir::homePath()));
    }
};

QTEST_GUILESS_MAIN(TestLocalFileSource)
#include "tst_localfilesource.moc"
