#include "core/hotkeys.h"

#include <QSettings>

#include <algorithm>

namespace bifrons {

namespace {

const QString Group = QStringLiteral("hotkeys");

QStringList cleaned(const QStringList &keys)
{
    QStringList out;
    for (const QString &key : keys) {
        const QString k = key.trimmed();
        if (!k.isEmpty() && !out.contains(k)) {
            out.append(k);
        }
    }
    return out;
}

} // namespace

HotkeyMap loadHotkeys(QSettings &settings)
{
    HotkeyMap map;
    settings.beginGroup(Group);
    const QStringList names = settings.childKeys();
    for (const QString &name : names) {
        // An empty value reads as one empty string, which cleaned() turns into "no key".
        map.insert(name, cleaned(settings.value(name).toStringList()));
    }
    settings.endGroup();
    return map;
}

void saveHotkeys(QSettings &settings, const HotkeyMap &effective, const HotkeyMap &defaults)
{
    settings.beginGroup(Group);
    settings.remove(QString());
    for (auto it = effective.constBegin(); it != effective.constEnd(); ++it) {
        const QStringList keys = cleaned(it.value());
        if (keys == cleaned(defaults.value(it.key()))) {
            continue;
        }
        // A single empty string keeps the entry, so "no key" survives a reload.
        settings.setValue(it.key(), keys.isEmpty() ? QVariant(QString()) : QVariant(keys));
    }
    settings.endGroup();
}

HotkeyMap mergeHotkeys(const HotkeyMap &defaults, const HotkeyMap &overrides)
{
    HotkeyMap merged = defaults;
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
        if (merged.contains(it.key())) {
            merged.insert(it.key(), cleaned(it.value()));
        }
    }
    return merged;
}

QHash<QString, QStringList> findClashes(const HotkeyMap &map)
{
    QHash<QString, QStringList> users;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        for (const QString &key : cleaned(it.value())) {
            users[key].append(it.key());
        }
    }
    QHash<QString, QStringList> clashes;
    for (auto it = users.begin(); it != users.end(); ++it) {
        if (it.value().size() > 1) {
            QStringList names = it.value();
            std::sort(names.begin(), names.end());
            clashes.insert(it.key(), names);
        }
    }
    return clashes;
}

} // namespace bifrons
