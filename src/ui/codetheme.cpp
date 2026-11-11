#include "ui/codetheme.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QStyleHints>

namespace bifrons {

CodeTheme CodeTheme::current()
{
    const bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    if (dark) {
        return {QColor(0x0d1117), QColor(0xe6edf3), QColor(0x9198a1), QColor(0x161b22), QColor(0x3d444d),
                QColor(0x4493f8), QColor(0xff7b72), QColor(0xffa657), QColor(0xa5d6ff), QColor(0x9198a1),
                QColor(0x79c0ff), QColor(0xd2a8ff), QColor(0x7ee787), QColor(0x79c0ff)};
    }
    return {QColor(0xffffff), QColor(0x1f2328), QColor(0x59636e), QColor(0xf6f8fa), QColor(0xd1d9e0),
            QColor(0x0969da), QColor(0xcf222e), QColor(0x953800), QColor(0x0a3069), QColor(0x59636e),
            QColor(0x0550ae), QColor(0x8250df), QColor(0x116329), QColor(0x0550ae)};
}

QColor CodeTheme::colourFor(TokenKind kind) const
{
    switch (kind) {
    case TokenKind::Keyword:
        return keyword;
    case TokenKind::Type:
        return type;
    case TokenKind::String:
        return string;
    case TokenKind::Comment:
        return comment;
    case TokenKind::Number:
        return number;
    case TokenKind::Preprocessor:
        return preproc;
    case TokenKind::Tag:
        return tag;
    case TokenKind::Attribute:
        return attr;
    }
    return fg;
}

QString languageForFile(const QString &fileName)
{
    const QFileInfo info(fileName);
    if (info.fileName().compare(QLatin1String("CMakeLists.txt"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("cmake");
    }
    const QString suffix = info.suffix().toLower();
    if (suffix == QLatin1String("pp") || suffix == QLatin1String("inc")) {
        return QStringLiteral("pascal");
    }
    return isKnownLanguage(suffix) ? suffix : QString();
}

} // namespace bifrons
