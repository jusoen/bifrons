#pragma once

#include <QHash>
#include <QStringList>

class QSettings;

namespace bifrons {

// Keys per cm_* command, each key in QKeySequence portable text ("Ctrl+F", "F8"). An empty list
// means the command has no key.
using HotkeyMap = QHash<QString, QStringList>;

// Reads the "hotkeys" group. Only commands listed there are returned, as overrides of the defaults.
HotkeyMap loadHotkeys(QSettings &settings);
// Replaces the "hotkeys" group with the commands in effective whose keys differ from defaults.
void saveHotkeys(QSettings &settings, const HotkeyMap &effective, const HotkeyMap &defaults);
// Defaults with overrides applied. Overrides for commands not in defaults are ignored.
HotkeyMap mergeHotkeys(const HotkeyMap &defaults, const HotkeyMap &overrides);
// Keys bound to more than one command, each with the commands using it in sorted order.
QHash<QString, QStringList> findClashes(const HotkeyMap &map);

} // namespace bifrons
