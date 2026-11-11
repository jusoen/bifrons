#include "ui/commandregistry.h"

#include <QAction>
#include <QWidget>

namespace bifrons {

CommandRegistry::CommandRegistry(QWidget *owner)
    : QObject(owner)
    , m_owner(owner)
{
}

QAction *CommandRegistry::add(const QString &name, const QString &text, const QKeySequence &key,
                              std::function<void()> handler)
{
    Q_ASSERT(!m_actions.contains(name));
    auto *action = new QAction(text, m_owner);
    action->setObjectName(name);
    action->setShortcut(key);
    action->setShortcutContext(Qt::WindowShortcut);
    connect(action, &QAction::triggered, this, [handler = std::move(handler)] { handler(); });
    m_owner->addAction(action);
    m_actions.insert(name, action);
    return action;
}

QAction *CommandRegistry::action(const QString &name) const
{
    return m_actions.value(name, nullptr);
}

} // namespace bifrons
