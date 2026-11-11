#pragma once

#include <QString>

namespace dcpp {

// Opens the platform terminal in dir. Returns false and sets error if nothing could start.
bool openTerminal(const QString &dir, QString *error);

} // namespace dcpp
