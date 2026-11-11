#pragma once

#include <QColor>
#include <QString>

#include "core/codehighlight.h"

namespace bifrons {

// GitHub light/dark colours, chosen from the current Windows colour scheme.
struct CodeTheme
{
    QColor bg, fg, muted, codeBg, border, link;
    QColor keyword, type, string, comment, number, preproc, tag, attr;

    QColor colourFor(TokenKind kind) const;
    static CodeTheme current();
};

// Highlighter language for a file name, or empty if none applies.
QString languageForFile(const QString &fileName);

} // namespace bifrons
