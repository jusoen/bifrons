#include <QtTest>

#include "core/filesort.h"

using namespace bifrons;

namespace {

FileItem file(const QString &name, qint64 size = 0)
{
    FileItem item;
    item.name = name;
    item.size = size;
    return item;
}

FileItem dir(const QString &name)
{
    FileItem item;
    item.name = name;
    item.isDir = true;
    return item;
}

QStringList names(const FileItemList &items)
{
    QStringList out;
    for (const FileItem &item : items) {
        out.append(item.name);
    }
    return out;
}

} // namespace

class TestFileSort : public QObject
{
    Q_OBJECT

private slots:
    void directoriesFirstNaturalOrder()
    {
        FileItemList items{file(QStringLiteral("file10")), dir(QStringLiteral("zdir")),
                           file(QStringLiteral("File2")), dir(QStringLiteral("adir"))};
        sortFileItems(items, SortColumn::Name, Qt::AscendingOrder);
        QCOMPARE(names(items), (QStringList{"adir", "zdir", "File2", "file10"}));
    }

    void descendingKeepsDirectoriesFirst()
    {
        FileItemList items{file(QStringLiteral("a")), dir(QStringLiteral("d")), file(QStringLiteral("b"))};
        sortFileItems(items, SortColumn::Name, Qt::DescendingOrder);
        QCOMPARE(names(items), (QStringList{"d", "b", "a"}));
    }

    void bySizeDirectoriesByName()
    {
        FileItemList items{file(QStringLiteral("big"), 300), dir(QStringLiteral("b")),
                           file(QStringLiteral("small"), 1), dir(QStringLiteral("a"))};
        sortFileItems(items, SortColumn::Size, Qt::AscendingOrder);
        QCOMPARE(names(items), (QStringList{"a", "b", "small", "big"}));
    }

    void byExtension()
    {
        FileItemList items{file(QStringLiteral("b.txt")), file(QStringLiteral("a.zip")),
                           file(QStringLiteral("c.cpp"))};
        sortFileItems(items, SortColumn::Extension, Qt::AscendingOrder);
        QCOMPARE(names(items), (QStringList{"c.cpp", "b.txt", "a.zip"}));
    }

    void extensionRules()
    {
        QCOMPARE(file(QStringLiteral("a.tar.gz")).extension(), QStringLiteral("gz"));
        QCOMPARE(file(QStringLiteral(".bashrc")).extension(), QString());
        QCOMPARE(file(QStringLiteral("trailing.")).extension(), QString());
        QCOMPARE(dir(QStringLiteral("my.dir")).extension(), QString());
        QCOMPARE(file(QStringLiteral("a.tar.gz")).baseName(), QStringLiteral("a.tar"));
    }
};

QTEST_GUILESS_MAIN(TestFileSort)
#include "tst_filesort.moc"
