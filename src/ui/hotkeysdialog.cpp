#include "ui/hotkeysdialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace bifrons {

namespace {

enum Column { CaptionColumn, NameColumn, KeysColumn, ColumnCount };

// Keys the file list handles itself. As window-wide shortcuts they would stop working in the list.
bool isReserved(const QKeySequence &seq)
{
    if (seq.count() != 1) {
        return false;
    }
    const QKeyCombination combo = seq[0];
    const Qt::Key key = combo.key();
    const Qt::KeyboardModifiers mods = combo.keyboardModifiers() & ~Qt::KeypadModifier;
    static const QList<Qt::Key> plain = {Qt::Key_Return, Qt::Key_Enter,  Qt::Key_Tab,      Qt::Key_Backspace,
                                         Qt::Key_Space,  Qt::Key_Insert, Qt::Key_Up,       Qt::Key_Down,
                                         Qt::Key_Left,   Qt::Key_Right,  Qt::Key_PageUp,   Qt::Key_PageDown,
                                         Qt::Key_Home,   Qt::Key_End,    Qt::Key_Escape,   Qt::Key_Plus,
                                         Qt::Key_Minus};
    static const QList<Qt::Key> shifted = {Qt::Key_Up,     Qt::Key_Down, Qt::Key_PageUp,
                                           Qt::Key_PageDown, Qt::Key_Home, Qt::Key_End, Qt::Key_Backtab};
    if (mods == Qt::NoModifier) {
        return plain.contains(key);
    }
    if (mods == Qt::ShiftModifier) {
        return shifted.contains(key);
    }
    if (mods == Qt::ControlModifier) {
        return key == Qt::Key_A || key == Qt::Key_PageUp;
    }
    return false;
}

QString nativeText(const QString &portable)
{
    return QKeySequence::fromString(portable, QKeySequence::PortableText).toString(QKeySequence::NativeText);
}

} // namespace

HotkeysDialog::HotkeysDialog(const QList<Command> &commands, const HotkeyMap &current, const HotkeyMap &defaults,
                             QWidget *parent)
    : QDialog(parent)
    , m_commands(commands)
    , m_map(current)
    , m_defaults(defaults)
{
    setWindowTitle(tr("Hot Keys"));
    std::sort(m_commands.begin(), m_commands.end(), [](const Command &a, const Command &b) {
        return a.caption.compare(b.caption, Qt::CaseInsensitive) < 0;
    });

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search commands or keys"));
    m_search->setClearButtonEnabled(true);

    m_table = new QTableWidget(static_cast<int>(m_commands.size()), ColumnCount, this);
    m_table->setHorizontalHeaderLabels({tr("Command"), tr("Name"), tr("Keys")});
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    for (int row = 0; row < m_commands.size(); ++row) {
        m_table->setItem(row, CaptionColumn, new QTableWidgetItem(m_commands.at(row).caption));
        m_table->setItem(row, NameColumn, new QTableWidgetItem(m_commands.at(row).name));
        m_table->setItem(row, KeysColumn, new QTableWidgetItem);
    }
    refreshAll();
    m_table->resizeColumnsToContents();

    m_keyEdit = new QKeySequenceEdit(this);
    m_keyEdit->setMaximumSequenceLength(1);
    m_keyEdit->setClearButtonEnabled(true);
    auto *add = new QPushButton(tr("&Add Key"), this);
    auto *clear = new QPushButton(tr("&Clear Keys"), this);
    auto *reset = new QPushButton(tr("&Reset to Default"), this);
    auto *keyRow = new QHBoxLayout;
    keyRow->addWidget(new QLabel(tr("Press a key:"), this));
    keyRow->addWidget(m_keyEdit, 1);
    keyRow->addWidget(add);
    keyRow->addWidget(clear);
    keyRow->addWidget(reset);

    m_note = new QLabel(this);
    m_note->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_search);
    layout->addWidget(m_table, 1);
    layout->addLayout(keyRow);
    layout->addWidget(m_note);
    layout->addWidget(buttons);

    connect(m_search, &QLineEdit::textChanged, this, &HotkeysDialog::applySearch);
    connect(add, &QPushButton::clicked, this, &HotkeysDialog::addKey);
    connect(clear, &QPushButton::clicked, this, &HotkeysDialog::clearKeys);
    connect(reset, &QPushButton::clicked, this, &HotkeysDialog::resetKeys);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] { m_note->clear(); });

    constexpr int StartWidth = 720;
    constexpr int StartHeight = 560;
    resize(StartWidth, StartHeight);
    if (!m_commands.isEmpty()) {
        m_table->selectRow(0);
    }
}

HotkeyMap HotkeysDialog::hotkeys() const
{
    return m_map;
}

QString HotkeysDialog::selectedName() const
{
    const QList<QTableWidgetItem *> items = m_table->selectedItems();
    if (items.isEmpty()) {
        return QString();
    }
    return m_commands.at(items.first()->row()).name;
}

void HotkeysDialog::refreshRow(int row)
{
    QStringList shown;
    for (const QString &key : m_map.value(m_commands.at(row).name)) {
        shown.append(nativeText(key));
    }
    m_table->item(row, KeysColumn)->setText(shown.join(QStringLiteral(", ")));
}

void HotkeysDialog::refreshAll()
{
    for (int row = 0; row < m_commands.size(); ++row) {
        refreshRow(row);
    }
}

void HotkeysDialog::applySearch(const QString &text)
{
    const QString needle = text.trimmed();
    for (int row = 0; row < m_commands.size(); ++row) {
        bool match = needle.isEmpty();
        for (int column = 0; !match && column < ColumnCount; ++column) {
            match = m_table->item(row, column)->text().contains(needle, Qt::CaseInsensitive);
        }
        m_table->setRowHidden(row, !match);
    }
}

void HotkeysDialog::addKey()
{
    const QString name = selectedName();
    const QKeySequence seq = m_keyEdit->keySequence();
    if (name.isEmpty() || seq.isEmpty()) {
        m_note->setText(tr("Select a command, then press the key to add."));
        return;
    }
    if (isReserved(seq)) {
        m_note->setText(tr("%1 is used by the file list, so it cannot be a shortcut.")
                            .arg(seq.toString(QKeySequence::NativeText)));
        return;
    }
    const QString key = seq.toString(QKeySequence::PortableText);
    QStringList others;
    for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it) {
        if (it.key() != name && it.value().contains(key)) {
            others.append(it.key());
        }
    }
    if (!others.isEmpty()) {
        QStringList captions;
        for (const Command &c : std::as_const(m_commands)) {
            if (others.contains(c.name)) {
                captions.append(c.caption);
            }
        }
        const auto answer = QMessageBox::question(
            this, tr("Hot Keys"),
            tr("%1 is already used by %2. Move it to this command?")
                .arg(seq.toString(QKeySequence::NativeText), captions.join(QStringLiteral(", "))));
        if (answer != QMessageBox::Yes) {
            return;
        }
        for (const QString &other : std::as_const(others)) {
            m_map[other].removeAll(key);
        }
    }
    if (!m_map[name].contains(key)) {
        m_map[name].append(key);
    }
    m_keyEdit->clear();
    m_note->clear();
    refreshAll();
}

void HotkeysDialog::clearKeys()
{
    const QString name = selectedName();
    if (name.isEmpty()) {
        return;
    }
    m_map[name].clear();
    refreshAll();
}

void HotkeysDialog::resetKeys()
{
    const QString name = selectedName();
    if (name.isEmpty()) {
        return;
    }
    const QStringList keys = m_defaults.value(name);
    // A default key now used elsewhere is moved back, so two commands never share one.
    for (auto it = m_map.begin(); it != m_map.end(); ++it) {
        if (it.key() != name) {
            for (const QString &key : keys) {
                it.value().removeAll(key);
            }
        }
    }
    m_map[name] = keys;
    m_note->clear();
    refreshAll();
}

} // namespace bifrons
