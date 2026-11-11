#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>

namespace dcpp {

// Case-insensitive ';'-separated wildcard masks, e.g. "*.cpp;*.h".
class FileMask
{
public:
    explicit FileMask(const QString &masks = QStringLiteral("*"));

    bool matches(const QString &name) const;
    bool isValid() const;

private:
    QList<QRegularExpression> m_patterns;
};

} // namespace dcpp
