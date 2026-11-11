#include "ui/favouritesdialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/localfilesource.h"

namespace dcpp {

FavouritesDialog::FavouritesDialog(const FavouriteList &list, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Organise Favourites"));
    resize(640, 420);

    m_table = new QTableWidget(0, 2, this);
    m_table->setHorizontalHeaderLabels({tr("Name"), tr("Folder")});
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(0, 200);
    for (const Favourite &fav : list) {
        addRow(fav, m_table->rowCount());
    }

    auto *add = new QPushButton(tr("Add…"), this);
    auto *remove = new QPushButton(tr("Remove"), this);
    auto *up = new QPushButton(tr("Move up"), this);
    auto *down = new QPushButton(tr("Move down"), this);
    auto *side = new QVBoxLayout;
    for (QPushButton *b : {add, remove, up, down}) {
        b->setAutoDefault(false);
        side->addWidget(b);
    }
    side->addStretch();

    auto *body = new QHBoxLayout;
    body->addWidget(m_table, 1);
    body->addLayout(side);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(body, 1);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(up, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(down, &QPushButton::clicked, this, [this] { moveSelected(1); });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        if (row >= 0) {
            m_table->removeRow(row);
        }
    });
    connect(add, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Add favourite"));
        if (dir.isEmpty()) {
            return;
        }
        const QString path = LocalFileSource::normalise(dir);
        const QString name = QDir(path).dirName();
        addRow({name.isEmpty() ? QDir::toNativeSeparators(path) : name, path}, m_table->rowCount());
        m_table->selectRow(m_table->rowCount() - 1);
    });
}

void FavouritesDialog::addRow(const Favourite &fav, int row)
{
    m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem(fav.name));
    m_table->setItem(row, 1, new QTableWidgetItem(QDir::toNativeSeparators(fav.path)));
}

void FavouritesDialog::moveSelected(int delta)
{
    const int row = m_table->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= m_table->rowCount()) {
        return;
    }
    const Favourite fav{m_table->item(row, 0)->text(), m_table->item(row, 1)->text()};
    m_table->removeRow(row);
    addRow(fav, target);
    m_table->selectRow(target);
}

FavouriteList FavouritesDialog::favourites() const
{
    FavouriteList list;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QString path = m_table->item(row, 1)->text().trimmed();
        if (path.isEmpty()) {
            continue;
        }
        const QString clean = LocalFileSource::normalise(path);
        QString name = m_table->item(row, 0)->text().trimmed();
        if (name.isEmpty()) {
            name = QDir(clean).dirName();
        }
        list.append({name, clean});
    }
    return list;
}

} // namespace dcpp
