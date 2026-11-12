#include <QtTest>

#include "core/localfilesource.h"
#include "core/transfer.h"

using namespace bifrons;

class TestTransfer : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tmp;
    QString m_src;
    QString m_dst;

    QString touch(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            return QString();
        }
        f.write("x");
        return path;
    }

private slots:
    void init()
    {
        QVERIFY(m_tmp.isValid());
        m_src = LocalFileSource::normalise(m_tmp.filePath(QStringLiteral("src")));
        m_dst = LocalFileSource::normalise(m_tmp.filePath(QStringLiteral("dst")));
        QVERIFY(QDir().mkpath(m_src));
        QVERIFY(QDir().mkpath(m_dst));
    }

    void intoExistingDirectory()
    {
        const QString a = touch(m_src + QStringLiteral("/a.txt"));
        const TransferPlan plan = planTransfer({a}, m_dst, m_src);
        QVERIFY(plan.ok);
        QVERIFY(plan.createDir.isEmpty());
        QCOMPARE(plan.items.size(), 1);
        QCOMPARE(plan.items.first().target, m_dst + QStringLiteral("/a.txt"));
    }

    void singleItemNewName()
    {
        const QString a = touch(m_src + QStringLiteral("/a.txt"));
        const TransferPlan plan = planTransfer({a}, QStringLiteral("b.txt"), m_src);
        QVERIFY(plan.ok);
        QCOMPARE(plan.items.first().target, m_src + QStringLiteral("/b.txt"));
    }

    void severalItemsCreateDirectory()
    {
        const QString a = touch(m_src + QStringLiteral("/a"));
        const QString b = touch(m_src + QStringLiteral("/b"));
        const QString newDir = m_dst + QStringLiteral("/new");
        const TransferPlan plan = planTransfer({a, b}, newDir, m_src);
        QVERIFY(plan.ok);
        QCOMPARE(plan.createDir, newDir);
        QCOMPARE(plan.items.at(1).target, newDir + QStringLiteral("/b"));
    }

    void trailingSlashMeansDirectory()
    {
        const QString a = touch(m_src + QStringLiteral("/a"));
        const TransferPlan plan = planTransfer({a}, m_dst + QStringLiteral("/sub/"), m_src);
        QVERIFY(plan.ok);
        QCOMPARE(plan.createDir, m_dst + QStringLiteral("/sub"));
        QCOMPARE(plan.items.first().target, m_dst + QStringLiteral("/sub/a"));
    }

    void severalItemsOntoFileFails()
    {
        const QString a = touch(m_src + QStringLiteral("/a"));
        const QString b = touch(m_src + QStringLiteral("/b"));
        const QString file = touch(m_dst + QStringLiteral("/file"));
        QVERIFY(!planTransfer({a, b}, file, m_src).ok);
    }

    void emptyInputsFail()
    {
        QVERIFY(!planTransfer({}, m_dst, m_src).ok);
        QVERIFY(!planTransfer({m_src}, QStringLiteral("  "), m_src).ok);
    }

    void targetElsewhereIsFine()
    {
        QVERIFY(transferTargetProblem({QStringLiteral("C:/a/x.txt")}, QStringLiteral("C:/b")).isEmpty());
        // One item from elsewhere is enough to make the copy worth doing.
        QVERIFY(transferTargetProblem({QStringLiteral("C:/b/x.txt"), QStringLiteral("C:/a/y.txt")},
                                      QStringLiteral("C:/b"))
                    .isEmpty());
    }

    void sameFolderIsRefused()
    {
        QVERIFY(!transferTargetProblem({QStringLiteral("C:/a/x.txt")}, QStringLiteral("c:/A/")).isEmpty());
        QVERIFY(!transferTargetProblem({QStringLiteral("C:\\a\\x.txt")}, QStringLiteral("C:/a")).isEmpty());
        QVERIFY(!transferTargetProblem({QStringLiteral("C:/x.txt")}, QStringLiteral("C:/")).isEmpty());
        QVERIFY(!transferTargetProblem({QStringLiteral("//srv/share/x")}, QStringLiteral("//srv/share")).isEmpty());
    }

    void folderIntoItselfIsRefused()
    {
        QVERIFY(!transferTargetProblem({QStringLiteral("C:/a")}, QStringLiteral("C:/a")).isEmpty());
        QVERIFY(!transferTargetProblem({QStringLiteral("C:/a")}, QStringLiteral("C:/A/b/c")).isEmpty());
        // A sibling whose name only starts the same is not inside.
        QVERIFY(transferTargetProblem({QStringLiteral("C:/a")}, QStringLiteral("C:/ab")).isEmpty());
    }

    void emptyTargetProblemInputs()
    {
        QVERIFY(!transferTargetProblem({}, QStringLiteral("C:/a")).isEmpty());
        QVERIFY(!transferTargetProblem({QStringLiteral("C:/a/x")}, QString()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestTransfer)
#include "tst_transfer.moc"
