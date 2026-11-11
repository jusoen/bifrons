#include "ui/multirenamedialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace dcpp {

namespace {

constexpr int MaxCounter = 1'000'000'000;
constexpr int MaxCounterWidth = 10;

} // namespace

MultiRenameDialog::MultiRenameDialog(QList<RenameItem> items, bool canUndo, QWidget *parent)
    : QDialog(parent)
    , m_items(std::move(items))
{
    setWindowTitle(tr("Multi-Rename (%1 items)").arg(m_items.size()));
    resize(800, 600);

    m_nameMask = new QLineEdit(QStringLiteral("[N]"), this);
    m_extMask = new QLineEdit(QStringLiteral("[E]"), this);
    auto *maskRow = new QHBoxLayout;
    maskRow->addWidget(new QLabel(tr("Name:"), this));
    maskRow->addWidget(m_nameMask, 3);
    maskRow->addWidget(insertButton(m_nameMask));
    maskRow->addSpacing(12);
    maskRow->addWidget(new QLabel(tr("Extension:"), this));
    maskRow->addWidget(m_extMask, 1);
    maskRow->addWidget(insertButton(m_extMask));

    m_find = new QLineEdit(this);
    m_replace = new QLineEdit(this);
    m_regex = new QCheckBox(tr("Regular expression"), this);
    m_findCase = new QCheckBox(tr("Case sensitive"), this);
    auto *findRow = new QHBoxLayout;
    findRow->addWidget(new QLabel(tr("Find:"), this));
    findRow->addWidget(m_find, 1);
    findRow->addWidget(new QLabel(tr("Replace with:"), this));
    findRow->addWidget(m_replace, 1);
    findRow->addWidget(m_regex);
    findRow->addWidget(m_findCase);

    m_case = new QComboBox(this);
    m_case->addItem(tr("Unchanged"), static_cast<int>(RenameOptions::Case::Unchanged));
    m_case->addItem(tr("lower case"), static_cast<int>(RenameOptions::Case::Lower));
    m_case->addItem(tr("UPPER CASE"), static_cast<int>(RenameOptions::Case::Upper));
    m_case->addItem(tr("First letter upper"), static_cast<int>(RenameOptions::Case::FirstUpper));
    m_case->addItem(tr("Each Word Upper"), static_cast<int>(RenameOptions::Case::EachWord));
    m_counterStart = new QSpinBox(this);
    m_counterStart->setRange(-MaxCounter, MaxCounter);
    m_counterStart->setValue(1);
    m_counterStep = new QSpinBox(this);
    m_counterStep->setRange(-MaxCounter, MaxCounter);
    m_counterStep->setValue(1);
    m_counterWidth = new QSpinBox(this);
    m_counterWidth->setRange(1, MaxCounterWidth);
    auto *optionRow = new QHBoxLayout;
    optionRow->addWidget(new QLabel(tr("Case:"), this));
    optionRow->addWidget(m_case);
    optionRow->addSpacing(12);
    optionRow->addWidget(new QLabel(tr("Counter [C] start:"), this));
    optionRow->addWidget(m_counterStart);
    optionRow->addWidget(new QLabel(tr("step:"), this));
    optionRow->addWidget(m_counterStep);
    optionRow->addWidget(new QLabel(tr("digits:"), this));
    optionRow->addWidget(m_counterWidth);
    optionRow->addStretch();

    m_table = new QTableWidget(static_cast<int>(m_items.size()), 3, this);
    m_table->setHorizontalHeaderLabels({tr("Old name"), tr("New name"), tr("Problem")});
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(0, 280);
    m_table->setColumnWidth(1, 280);
    for (int i = 0; i < m_items.size(); ++i) {
        auto *old = new QTableWidgetItem(QFileInfo(m_items.at(i).path).fileName());
        old->setToolTip(QDir::toNativeSeparators(m_items.at(i).path));
        m_table->setItem(i, 0, old);
        m_table->setItem(i, 1, new QTableWidgetItem);
        m_table->setItem(i, 2, new QTableWidgetItem);
    }

    m_status = new QLabel(this);
    m_rename = new QPushButton(tr("Rename"), this);
    m_rename->setDefault(true);
    auto *undo = new QPushButton(tr("Undo last rename"), this);
    undo->setAutoDefault(false);
    undo->setEnabled(canUndo);
    auto *cancel = new QPushButton(tr("Close"), this);
    cancel->setAutoDefault(false);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_status, 1);
    buttons->addWidget(undo);
    buttons->addWidget(m_rename);
    buttons->addWidget(cancel);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(maskRow);
    layout->addLayout(findRow);
    layout->addLayout(optionRow);
    layout->addWidget(m_table, 1);
    layout->addLayout(buttons);

    for (QLineEdit *edit : {m_nameMask, m_extMask, m_find, m_replace}) {
        connect(edit, &QLineEdit::textChanged, this, &MultiRenameDialog::updatePreview);
    }
    for (QCheckBox *box : {m_regex, m_findCase}) {
        connect(box, &QCheckBox::toggled, this, &MultiRenameDialog::updatePreview);
    }
    connect(m_case, &QComboBox::currentIndexChanged, this, &MultiRenameDialog::updatePreview);
    for (QSpinBox *spin : {m_counterStart, m_counterStep, m_counterWidth}) {
        connect(spin, &QSpinBox::valueChanged, this, &MultiRenameDialog::updatePreview);
    }
    connect(m_rename, &QPushButton::clicked, this, &MultiRenameDialog::rename);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(undo, &QPushButton::clicked, this, [this] {
        m_undoRequested = true;
        accept();
    });

    updatePreview();
}

QPushButton *MultiRenameDialog::insertButton(QLineEdit *target)
{
    auto *button = new QPushButton(tr("+"), this);
    button->setAutoDefault(false);
    button->setToolTip(tr("Insert placeholder"));
    button->setMaximumWidth(button->fontMetrics().horizontalAdvance(QStringLiteral("+++")) + 12);
    auto *menu = new QMenu(button);
    const struct
    {
        const char *token;
        const char *label;
    } placeholders[] = {
        {"[N]", "[N] Name"},
        {"[N1-3]", "[N1-3] Name characters 1 to 3"},
        {"[E]", "[E] Extension"},
        {"[C]", "[C] Counter"},
        {"[P]", "[P] Parent folder"},
        {"[Y]-[M]-[D]", "[Y]-[M]-[D] Modified date"},
        {"[h][n][s]", "[h][n][s] Modified time"},
    };
    for (const auto &p : placeholders) {
        const QString token = QLatin1String(p.token);
        connect(menu->addAction(tr(p.label)), &QAction::triggered, target, [target, token] {
            target->insert(token);
            target->setFocus();
        });
    }
    button->setMenu(menu);
    return button;
}

RenameOptions MultiRenameDialog::options() const
{
    RenameOptions o;
    o.nameMask = m_nameMask->text();
    o.extMask = m_extMask->text();
    o.find = m_find->text();
    o.replace = m_replace->text();
    o.regex = m_regex->isChecked();
    o.findCaseSensitive = m_findCase->isChecked();
    o.caseMode = static_cast<RenameOptions::Case>(m_case->currentData().toInt());
    o.counterStart = m_counterStart->value();
    o.counterStep = m_counterStep->value();
    o.counterWidth = m_counterWidth->value();
    return o;
}

void MultiRenameDialog::updatePreview()
{
    m_preview = previewRenames(m_items, options());
    int problems = 0;
    int changes = 0;
    for (int i = 0; i < m_items.size(); ++i) {
        const QString newName = i < m_preview.newNames.size() ? m_preview.newNames.at(i) : QString();
        const QString problem = i < m_preview.problems.size() ? m_preview.problems.at(i) : QString();
        m_table->item(i, 1)->setText(newName);
        m_table->item(i, 2)->setText(problem);
        m_table->item(i, 1)->setForeground(problem.isEmpty() ? palette().text() : QBrush(Qt::red));
        if (!problem.isEmpty()) {
            ++problems;
        }
        if (!newName.isEmpty() && newName != m_table->item(i, 0)->text()) {
            ++changes;
        }
    }
    if (!m_preview.error.isEmpty()) {
        m_status->setText(m_preview.error);
    } else if (problems > 0) {
        m_status->setText(tr("%1 problem(s). Fix them to rename.").arg(problems));
    } else {
        m_status->setText(tr("%1 of %2 will change.").arg(changes).arg(m_items.size()));
    }
    m_rename->setEnabled(m_preview.ok() && changes > 0);
}

void MultiRenameDialog::rename()
{
    // Re-check against the disk right before renaming, in case it changed.
    updatePreview();
    if (!m_rename->isEnabled()) {
        return;
    }
    const RenameResult result = applyRenames(m_items, m_preview.newNames);
    if (!result.ok) {
        QMessageBox::warning(this, tr("Multi-Rename"),
                             tr("Nothing was renamed.\n\n%1").arg(result.error));
        return;
    }
    m_performed = result.done;
    accept();
}

QList<RenameStep> MultiRenameDialog::performed() const
{
    return m_performed;
}

bool MultiRenameDialog::undoRequested() const
{
    return m_undoRequested;
}

} // namespace dcpp
