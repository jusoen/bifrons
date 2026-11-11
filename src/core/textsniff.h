#pragma once

#include <QByteArray>
#include <QString>

namespace bifrons {

// True when the start of a file looks like text rather than binary data.
bool looksLikeText(const QByteArray &head);

// BOM, else strict UTF-8, else the system code page. truncated: data is the start of a longer file.
QString decodeText(const QByteArray &data, QString *encodingName, bool truncated = false);

} // namespace bifrons
