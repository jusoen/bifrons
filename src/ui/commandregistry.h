#pragma once

#include <QHash>
#include <QKeySequence>
#include <QObject>

#include <functional>

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

private:
    QWidget *m_owner;
    QHash<QString, QAction *> m_actions;
};

} // namespace bifrons
