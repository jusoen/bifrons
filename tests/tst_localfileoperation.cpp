#include <QtTest>

#include "core/localfileoperation.h"

using namespace bifrons;
using Answer = FileOperation::Answer;
using Kind = LocalFileOperation::Kind;

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

FileOperation::Result runOp(Kind kind, const QList<TransferItem> &items,
                            FileOperation::QueryHandler handler = nullptr)
{
    LocalFileOperation op(kind, items);
    if (handler) {
        op.setQueryHandler(std::move(handler));
    }
    op.run();
    return op.result();
}

} // namespace

class TestLocalFileOperation : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir *m_tmp = nullptr;
    QString m_src;
    QString m_dst;

private slots:
    void init()
    {
        m_tmp = new QTemporaryDir;
        QVERIFY(m_tmp->isValid());
        m_src = m_tmp->filePath(QStringLiteral("src"));
        m_dst = m_tmp->filePath(QStringLiteral("dst"));
        QVERIFY(QDir().mkpath(m_src + QStringLiteral("/sub/deeper")));
        QVERIFY(QDir().mkpath(m_dst));
        QVERIFY(writeFile(m_src + QStringLiteral("/a.txt"), "alpha"));
        QVERIFY(writeFile(m_src + QStringLiteral("/sub/b.txt"), "bravo"));
        QVERIFY(writeFile(m_src + QStringLiteral("/sub/deeper/c.txt"), "charlie"));
    }

    void cleanup()
    {
        delete m_tmp;
        m_tmp = nullptr;
    }

    void copiesTreeAndPreservesTime()
    {
        const QDateTime old = QDateTime::currentDateTime().addDays(-10);
        {
            QFile f(m_src + QStringLiteral("/a.txt"));
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(old, QFileDevice::FileModificationTime));
        }
        const FileOperation::Result r = runOp(Kind::Copy, {{m_src, m_dst + QStringLiteral("/copy")}});
        QVERIFY2(r.errors.isEmpty(), qPrintable(r.errors.join('\n')));
        QCOMPARE(r.processed, 3);
        QCOMPARE(readFile(m_dst + QStringLiteral("/copy/sub/deeper/c.txt")), QByteArray("charlie"));
        QVERIFY(QFileInfo::exists(m_src + QStringLiteral("/a.txt")));
        const qint64 drift = QFileInfo(m_dst + QStringLiteral("/copy/a.txt")).lastModified().secsTo(old);
        QVERIFY(qAbs(drift) <= 2);
        QVERIFY(!QFileInfo::exists(m_dst + QStringLiteral("/copy/a.txt.bifrons-part")));
    }

    void moveRenamesWholeTree()
    {
        const FileOperation::Result r = runOp(Kind::Move, {{m_src, m_dst + QStringLiteral("/moved")}});
        QVERIFY(r.errors.isEmpty());
        QVERIFY(!QFileInfo::exists(m_src));
        QCOMPARE(readFile(m_dst + QStringLiteral("/moved/sub/b.txt")), QByteArray("bravo"));
    }

    void moveMergesIntoExistingFolder()
    {
        QVERIFY(QDir().mkpath(m_dst + QStringLiteral("/merge/sub")));
        QVERIFY(writeFile(m_dst + QStringLiteral("/merge/keep.txt"), "keep"));
        const FileOperation::Result r = runOp(Kind::Move, {{m_src, m_dst + QStringLiteral("/merge")}});
        QVERIFY2(r.errors.isEmpty(), qPrintable(r.errors.join('\n')));
        QVERIFY(!QFileInfo::exists(m_src));
        QCOMPARE(readFile(m_dst + QStringLiteral("/merge/keep.txt")), QByteArray("keep"));
        QCOMPARE(readFile(m_dst + QStringLiteral("/merge/sub/deeper/c.txt")), QByteArray("charlie"));
    }

    void collisionSkipKeepsTarget()
    {
        const QString target = m_dst + QStringLiteral("/a.txt");
        QVERIFY(writeFile(target, "old"));
        int asked = 0;
        const FileOperation::Result r = runOp(Kind::Copy, {{m_src + QStringLiteral("/a.txt"), target}},
                                              [&](const FileOperation::Query &q) {
                                                  ++asked;
                                                  return q.kind == FileOperation::Query::Kind::Collision
                                                      ? Answer::Skip : Answer::Abort;
                                              });
        QCOMPARE(asked, 1);
        QCOMPARE(r.skipped, 1);
        QCOMPARE(readFile(target), QByteArray("old"));
    }

    void collisionOverwriteReplacesTarget()
    {
        const QString target = m_dst + QStringLiteral("/a.txt");
        QVERIFY(writeFile(target, "old"));
        const FileOperation::Result r = runOp(Kind::Copy, {{m_src + QStringLiteral("/a.txt"), target}},
                                              [](const FileOperation::Query &) { return Answer::Overwrite; });
        QVERIFY(r.errors.isEmpty());
        QCOMPARE(readFile(target), QByteArray("alpha"));
        QVERIFY(!QFileInfo::exists(target + QStringLiteral(".bifrons-bak")));
    }

    void collisionOverwriteReadOnlyTarget()
    {
        const QString target = m_dst + QStringLiteral("/a.txt");
        QVERIFY(writeFile(target, "old"));
        QVERIFY(QFile::setPermissions(target, QFile::ReadOwner | QFile::ReadUser));
        const FileOperation::Result r = runOp(Kind::Copy, {{m_src + QStringLiteral("/a.txt"), target}},
                                              [](const FileOperation::Query &) { return Answer::Overwrite; });
        QVERIFY2(r.errors.isEmpty(), qPrintable(r.errors.join('\n')));
        QCOMPARE(readFile(target), QByteArray("alpha"));
    }

    void collisionRenamePicksFreeName()
    {
        const QString target = m_dst + QStringLiteral("/a.txt");
        QVERIFY(writeFile(target, "old"));
        const FileOperation::Result r = runOp(Kind::Copy, {{m_src + QStringLiteral("/a.txt"), target}},
                                              [](const FileOperation::Query &) { return Answer::Rename; });
        QVERIFY(r.errors.isEmpty());
        QCOMPARE(readFile(target), QByteArray("old"));
        QCOMPARE(readFile(m_dst + QStringLiteral("/a (2).txt")), QByteArray("alpha"));
    }

    void noHandlerAbortsOnCollision()
    {
        const QString target = m_dst + QStringLiteral("/a.txt");
        QVERIFY(writeFile(target, "old"));
        const FileOperation::Result r = runOp(Kind::Copy, {{m_src + QStringLiteral("/a.txt"), target}});
        QVERIFY(r.aborted);
        QCOMPARE(readFile(target), QByteArray("old"));
    }

    void refusesCopyIntoItself()
    {
        const FileOperation::Result r = runOp(Kind::Copy, {{m_src, m_src + QStringLiteral("/sub/inside")}});
        QCOMPARE(r.skipped, 1);
        QVERIFY(!QFileInfo::exists(m_src + QStringLiteral("/sub/inside")));
    }

    void refusesCopyOntoItself()
    {
        const QString a = m_src + QStringLiteral("/a.txt");
        const FileOperation::Result r = runOp(Kind::Copy, {{a, a}});
        QCOMPARE(r.skipped, 1);
        QCOMPARE(readFile(a), QByteArray("alpha"));
    }

    void cancelLeavesNoPartial()
    {
        const QString big = m_src + QStringLiteral("/big.bin");
        QVERIFY(writeFile(big, QByteArray(3 * 1024 * 1024, 'z')));
        const QString target = m_dst + QStringLiteral("/big.bin");

        LocalFileOperation op(Kind::Copy, {{big, target}});
        connect(&op, &FileOperation::progress, &op, [&op](qint64, qint64, const QString &) { op.cancel(); });
        op.run();
        QVERIFY(op.result().aborted);
        QVERIFY(!QFileInfo::exists(target));
        QVERIFY(!QFileInfo::exists(target + QStringLiteral(".bifrons-part")));
    }

    void vanishedSourceIsSkipped()
    {
        const FileOperation::Result r =
            runOp(Kind::Copy, {{m_src + QStringLiteral("/missing"), m_dst + QStringLiteral("/missing")}});
        QCOMPARE(r.skipped, 1);
        QCOMPARE(r.errors.size(), 1);
    }

    void uniqueNames()
    {
        QVERIFY(writeFile(m_dst + QStringLiteral("/.bashrc"), "x"));
        QCOMPARE(QFileInfo(uniqueTargetName(m_dst + QStringLiteral("/.bashrc"))).fileName(),
                 QStringLiteral(".bashrc (2)"));
        QVERIFY(writeFile(m_dst + QStringLiteral("/a.tar.gz"), "x"));
        QCOMPARE(QFileInfo(uniqueTargetName(m_dst + QStringLiteral("/a.tar.gz"))).fileName(),
                 QStringLiteral("a.tar (2).gz"));
    }
};

QTEST_GUILESS_MAIN(TestLocalFileOperation)
#include "tst_localfileoperation.moc"
