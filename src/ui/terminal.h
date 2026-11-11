#pragma once

#include <QString>
#include <QStringList>

namespace bifrons {

// Opens the platform terminal in dir. Returns false and sets error if nothing could start.
bool openTerminal(const QString &dir, QString *error);

// Starts program with arguments in dir, detached. Batch files run through cmd, PowerShell scripts
// through powershell, and other non-programs through their Windows file association. A bare name
// is looked up on PATH. Returns false and sets error if it could not start.
bool launchProgram(const QString &program, const QStringList &arguments, const QString &dir, QString *error);

} // namespace bifrons
