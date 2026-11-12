#pragma once

#include <QDialog>
#include <QList>

#include "core/hotkeys.h"

class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QTableWidget;

namespace bifrons {

// Lists every command with its keys and lets the user add, clear or reset them.
// Changes apply only on OK.
class HotkeysDialog : public QDialog
{
    Q_OBJECT

public:
    struct Command
    {
        QString name;
        QString caption;
    };

    HotkeysDialog(const QList<Command> &commands, const HotkeyMap &current, const HotkeyMap &defaults,
                  QWidget *parent = nullptr);

    HotkeyMap hotkeys() const;

private:
    QString selectedName() const;
    void refreshRow(int row);
    void refreshAll();
    void applySearch(const QString &text);
    void addKey();
    void clearKeys();
    void resetKeys();

    QList<Command> m_commands;
    HotkeyMap m_map;
    HotkeyMap m_defaults;
    QLineEdit *m_search = nullptr;
    QTableWidget *m_table = nullptr;
    QKeySequenceEdit *m_keyEdit = nullptr;
    QLabel *m_note = nullptr;
};

} // namespace bifrons
