#include "core/markdownprep.h"

#include <QRegularExpression>
#include <QStringList>

namespace bifrons {

QString applyLabelBreaks(const QString &markdown)
{
    static const QRegularExpression label(QStringLiteral("^\\s{0,3}\\*\\*[^*\\n]+?(:\\*\\*|\\*\\*:)"));
    static const QRegularExpression fence(QStringLiteral("^\\s{0,3}(```|~~~)"));
    QStringList lines = markdown.split(QLatin1Char('\n'));
    bool inFence = false;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (fence.match(lines.at(i)).hasMatch()) {
            inFence = !inFence;
            continue;
        }
        if (inFence || i == 0) {
            continue;
        }
        QString &prev = lines[i - 1];
        const bool prevBlank = prev.trimmed().isEmpty();
        if (!prevBlank && label.match(lines.at(i)).hasMatch() && !prev.endsWith(QLatin1String("  "))) {
            // Two trailing spaces make a hard line break in CommonMark.
            const bool crlf = prev.endsWith(QLatin1Char('\r'));
            if (crlf) {
                prev.chop(1);
            }
            prev += QStringLiteral("  ");
            if (crlf) {
                prev += QLatin1Char('\r');
            }
        }
    }
    return lines.join(QLatin1Char('\n'));
}

QString headingSlug(const QString &heading)
{
    QString slug;
    for (const QChar c : heading.trimmed().toLower()) {
        if (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-')) {
            slug += c;
        } else if (c.isSpace()) {
            slug += QLatin1Char('-');
        }
    }
    return slug;
}

} // namespace bifrons
