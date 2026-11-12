#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>

namespace bifrons {

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

// Text typed into a panel's quick filter. Plain text matches anywhere in the name, ignoring case.
// Text containing * or ? is used as a FileMask instead. Empty text matches everything.
class QuickFilter
{
public:
    explicit QuickFilter(const QString &text = QString());

    bool isEmpty() const;
    bool matches(const QString &name) const;

private:
    QString m_text;
    bool m_wildcard = false;
    FileMask m_mask;
};

} // namespace bifrons
