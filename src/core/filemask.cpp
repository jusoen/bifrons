#include "core/filemask.h"

namespace bifrons {

FileMask::FileMask(const QString &masks)
{
    const QStringList parts = masks.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &raw : parts) {
        const QString mask = raw.trimmed();
        if (mask.isEmpty()) {
            continue;
        }
        // DOS convention: "*.*" also matches names without a dot.
        const QString effective = (mask == QLatin1String("*.*")) ? QStringLiteral("*") : mask;
        const QString pattern = QRegularExpression::wildcardToRegularExpression(
            effective, QRegularExpression::NonPathWildcardConversion);
        m_patterns.append(QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption));
    }
}

bool FileMask::matches(const QString &name) const
{
    for (const QRegularExpression &re : m_patterns) {
        if (re.match(name).hasMatch()) {
            return true;
        }
    }
    return false;
}

bool FileMask::isValid() const
{
    if (m_patterns.isEmpty()) {
        return false;
    }
    for (const QRegularExpression &re : m_patterns) {
        if (!re.isValid()) {
            return false;
        }
    }
    return true;
}

} // namespace bifrons
