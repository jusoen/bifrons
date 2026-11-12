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
    m_captions.insert(name, text.section(QLatin1Char('\t'), 0, 0));
    m_hints.insert(name, text.section(QLatin1Char('\t'), 1));
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

QStringList CommandRegistry::names() const
{
    QStringList list = m_actions.keys();
    list.sort();
    return list;
}

void CommandRegistry::captureDefaults()
{
    m_defaults = currentHotkeys();
}

HotkeyMap CommandRegistry::defaultHotkeys() const
{
    return m_defaults;
}

HotkeyMap CommandRegistry::currentHotkeys() const
{
    HotkeyMap map;
    for (auto it = m_actions.constBegin(); it != m_actions.constEnd(); ++it) {
        QStringList keys;
        for (const QKeySequence &seq : it.value()->shortcuts()) {
            keys.append(seq.toString(QKeySequence::PortableText));
        }
        map.insert(it.key(), keys);
    }
    return map;
}

void CommandRegistry::applyHotkeys(const HotkeyMap &map)
{
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        QAction *action = m_actions.value(it.key(), nullptr);
        if (action == nullptr) {
            continue;
        }
        QList<QKeySequence> seqs;
        for (const QString &key : it.value()) {
            const QKeySequence seq = QKeySequence::fromString(key, QKeySequence::PortableText);
            // A key Qt cannot parse is skipped rather than bound to nothing.
            if (!seq.isEmpty()) {
                seqs.append(seq);
            }
        }
        action->setShortcuts(seqs);
    }
    refreshCaptions();
}

QString CommandRegistry::keyText(const QString &name) const
{
    const QAction *action = m_actions.value(name, nullptr);
    if (action == nullptr) {
        return QString();
    }
    QStringList parts;
    if (const QString hint = m_hints.value(name); !hint.isEmpty()) {
        parts.append(hint);
    }
    for (const QKeySequence &seq : action->shortcuts()) {
        parts.append(seq.toString(QKeySequence::NativeText));
    }
    return parts.join(QStringLiteral(", "));
}

void CommandRegistry::refreshCaptions()
{
    for (auto it = m_actions.constBegin(); it != m_actions.constEnd(); ++it) {
        const QString keys = keyText(it.key());
        const QString caption = m_captions.value(it.key());
        // Text after a tab replaces the shortcut Qt would show, so every key gets listed.
        it.value()->setText(keys.isEmpty() ? caption : caption + QLatin1Char('\t') + keys);
    }
}

} // namespace bifrons
