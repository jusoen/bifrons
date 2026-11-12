#pragma once

#include <QHash>
#include <QKeySequence>
#include <QObject>

#include <functional>

#include "core/hotkeys.h"

class QAction;
class QWidget;

namespace bifrons {

// Named cm_* commands (Double Commander names) bound to window-wide shortcuts.
class CommandRegistry : public QObject
{
    Q_OBJECT

public:
    explicit CommandRegistry(QWidget *owner);

    QAction *add(const QString &name, const QString &text, const QKeySequence &key,
                 std::function<void()> handler);
    // Returns nullptr for an unknown name.
    QAction *action(const QString &name) const;
    // Command names in alphabetical order.
    QStringList names() const;

    // Records every command's current keys as its defaults. Call once all commands are added.
    void captureDefaults();
    HotkeyMap defaultHotkeys() const;
    HotkeyMap currentHotkeys() const;
    // Sets the keys of each command named in map. Commands not in map keep theirs.
    void applyHotkeys(const HotkeyMap &map);
    // Every key of a command as menus show it ("F5, Ctrl+C"), led by any fixed hint such as "+".
    QString keyText(const QString &name) const;
    // Rewrites each menu caption to list all its keys. Qt alone shows only the first.
    void refreshCaptions();

private:
    QWidget *m_owner;
    QHash<QString, QAction *> m_actions;
    HotkeyMap m_defaults;
    // Caption before any tab, and the hint after it, as given to add().
    QHash<QString, QString> m_captions;
    QHash<QString, QString> m_hints;
};

} // namespace bifrons
