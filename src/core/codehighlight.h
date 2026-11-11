#pragma once

#include <QList>
#include <QString>

namespace bifrons {

enum class TokenKind
{
    Keyword,
    Type,
    String,
    Comment,
    Number,
    Preprocessor,
    Tag,
    Attribute
};

struct CodeToken
{
    qsizetype start = 0;
    qsizetype length = 0;
    TokenKind kind = TokenKind::Keyword;
};

// Lightweight syntax tokens for a fenced code block. Empty for an unknown language.
QList<CodeToken> highlightCode(const QString &code, const QString &language);
bool isKnownLanguage(const QString &language);

} // namespace bifrons
