#pragma once

#include <QString>

namespace bifrons {

// As label-breaks.lua: hard break before a "**Label:**" line, other newlines stay soft.
QString applyLabelBreaks(const QString &markdown);

// GitHub-style heading anchor: "Code Block!" -> "code-block".
QString headingSlug(const QString &heading);

} // namespace bifrons
