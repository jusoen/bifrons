#include "core/codehighlight.h"

#include <QHash>
#include <QSet>
#include <QStringList>

namespace dcpp {

namespace {

struct Language
{
    QSet<QString> keywords;
    QSet<QString> types;
    QStringList lineComments;
    QList<QPair<QString, QString>> blockComments;
    QString quotes;
    bool caseInsensitive = false;
    bool hashPreprocessor = false;
    bool tripleQuotes = false;
    bool dollarIdentifiers = false;
    bool markup = false;
};

QSet<QString> words(const char *list)
{
    QSet<QString> set;
    for (const QString &w : QString::fromLatin1(list).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        set.insert(w);
    }
    return set;
}

QHash<QString, Language> buildLanguages()
{
    QHash<QString, Language> langs;

    Language cpp;
    cpp.keywords = words("alignas alignof auto break case catch class const constexpr consteval constinit const_cast "
                         "continue co_await co_return co_yield decltype default delete do dynamic_cast else enum "
                         "explicit export extern false final for friend goto if inline mutable namespace new noexcept "
                         "nullptr operator override private protected public register reinterpret_cast return "
                         "sizeof static static_assert static_cast struct switch template this thread_local throw "
                         "true try typedef typeid typename union using virtual volatile while");
    cpp.types = words("bool char char8_t char16_t char32_t double float int long short signed unsigned void wchar_t "
                      "size_t ssize_t ptrdiff_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t "
                      "uintptr_t intptr_t std string vector");
    cpp.lineComments = {QStringLiteral("//")};
    cpp.blockComments = {{QStringLiteral("/*"), QStringLiteral("*/")}};
    cpp.quotes = QStringLiteral("\"'");
    cpp.hashPreprocessor = true;
    langs.insert(QStringLiteral("cpp"), cpp);

    Language cs = cpp;
    cs.hashPreprocessor = true;
    cs.keywords = words("abstract as async await base break case catch checked class const continue default delegate "
                        "do else enum event explicit extern false finally fixed for foreach get goto if implicit in "
                        "init interface internal is lock namespace new null operator out override params private "
                        "protected public readonly record ref return sealed set sizeof stackalloc static struct switch "
                        "this throw true try typeof unchecked unsafe using value var virtual volatile when where while "
                        "yield");
    cs.types = words("bool byte char decimal double dynamic float int long nint nuint object sbyte short string uint "
                     "ulong ushort void");
    langs.insert(QStringLiteral("cs"), cs);

    Language java = cpp;
    java.hashPreprocessor = false;
    java.keywords = words("abstract assert break case catch class const continue default do else enum extends final "
                          "finally for goto if implements import instanceof interface native new null package private "
                          "protected public return static strictfp super switch synchronized this throw throws "
                          "transient try true false var volatile while record yield fun val when object companion");
    java.types = words("boolean byte char double float int long short void String Integer Long Boolean");
    langs.insert(QStringLiteral("java"), java);

    Language js;
    js.keywords = words("async await break case catch class const continue debugger default delete do else export "
                        "extends false finally for from function if import in instanceof let new null of return "
                        "static super switch this throw true try typeof undefined var void while with yield as "
                        "interface type enum implements private public protected readonly declare namespace");
    js.types = words("any boolean number string object never unknown symbol bigint Array Promise Map Set");
    js.lineComments = {QStringLiteral("//")};
    js.blockComments = {{QStringLiteral("/*"), QStringLiteral("*/")}};
    js.quotes = QStringLiteral("\"'`");
    js.dollarIdentifiers = true;
    langs.insert(QStringLiteral("js"), js);

    Language json;
    json.keywords = words("true false null");
    json.quotes = QStringLiteral("\"");
    langs.insert(QStringLiteral("json"), json);

    Language py;
    py.keywords = words("and as assert async await break class continue def del elif else except False finally for "
                        "from global if import in is lambda None nonlocal not or pass raise return True try while "
                        "with yield match case self");
    py.types = words("int float str bytes bool list dict set tuple object");
    py.lineComments = {QStringLiteral("#")};
    py.quotes = QStringLiteral("\"'");
    py.tripleQuotes = true;
    langs.insert(QStringLiteral("python"), py);

    Language sh;
    sh.keywords = words("if then else elif fi case esac for while until do done in function select return exit "
                        "export local readonly declare set unset shift source echo cd test true false");
    sh.lineComments = {QStringLiteral("#")};
    sh.quotes = QStringLiteral("\"'");
    sh.dollarIdentifiers = true;
    langs.insert(QStringLiteral("bash"), sh);

    Language ps;
    ps.caseInsensitive = true;
    ps.keywords = words("begin break catch class continue data define do dynamicparam else elseif end enum exit "
                        "filter finally for foreach from function if in param process return switch throw trap try "
                        "until using var while -eq -ne -gt -ge -lt -le -like -notlike -match -notmatch -and -or -not");
    ps.lineComments = {QStringLiteral("#")};
    ps.blockComments = {{QStringLiteral("<#"), QStringLiteral("#>")}};
    ps.quotes = QStringLiteral("\"'");
    ps.dollarIdentifiers = true;
    langs.insert(QStringLiteral("powershell"), ps);

    Language bat;
    bat.caseInsensitive = true;
    bat.keywords = words("if else for in do goto call exit echo set setlocal endlocal not exist defined errorlevel "
                         "equ neq lss leq gtr geq off on");
    bat.lineComments = {QStringLiteral("rem "), QStringLiteral("::")};
    bat.quotes = QStringLiteral("\"");
    langs.insert(QStringLiteral("bat"), bat);

    Language rust;
    rust.keywords = words("as async await break const continue crate dyn else enum extern false fn for if impl in let "
                          "loop match mod move mut pub ref return self Self static struct super trait true type "
                          "unsafe use where while");
    rust.types = words("i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 bool char str String Vec "
                       "Option Result Box");
    rust.lineComments = {QStringLiteral("//")};
    rust.blockComments = {{QStringLiteral("/*"), QStringLiteral("*/")}};
    rust.quotes = QStringLiteral("\"");
    langs.insert(QStringLiteral("rust"), rust);

    Language go;
    go.keywords = words("break case chan const continue default defer else fallthrough for func go goto if import "
                        "interface map package range return select struct switch type var true false nil iota");
    go.types = words("bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune string "
                     "uint uint8 uint16 uint32 uint64 uintptr any");
    go.lineComments = {QStringLiteral("//")};
    go.blockComments = {{QStringLiteral("/*"), QStringLiteral("*/")}};
    go.quotes = QStringLiteral("\"'`");
    langs.insert(QStringLiteral("go"), go);

    Language pas;
    pas.caseInsensitive = true;
    pas.keywords = words("and array as asm begin case class const constructor destructor div do downto else end "
                         "except exports file finalization finally for function goto if implementation in inherited "
                         "initialization inline interface is label library mod nil not object of or out packed "
                         "procedure program property raise record repeat resourcestring set shl shr string then "
                         "threadvar to try type unit until uses var while with xor true false self result override "
                         "virtual published private protected public");
    pas.types = words("integer cardinal byte word longint int64 boolean char real double single extended pointer "
                      "ansistring unicodestring");
    pas.lineComments = {QStringLiteral("//")};
    pas.blockComments = {{QStringLiteral("{"), QStringLiteral("}")}, {QStringLiteral("(*"), QStringLiteral("*)")}};
    pas.quotes = QStringLiteral("'");
    langs.insert(QStringLiteral("pascal"), pas);

    Language sql;
    sql.caseInsensitive = true;
    sql.keywords = words("select from where and or not insert into values update set delete create table drop alter "
                         "index view join inner left right outer on group by order having limit offset as distinct "
                         "union all null is in like between case when then else end primary key foreign references "
                         "default exists begin commit rollback");
    sql.types = words("int integer bigint smallint text varchar char date datetime timestamp boolean real float "
                      "double numeric decimal blob");
    sql.lineComments = {QStringLiteral("--")};
    sql.blockComments = {{QStringLiteral("/*"), QStringLiteral("*/")}};
    sql.quotes = QStringLiteral("'\"");
    langs.insert(QStringLiteral("sql"), sql);

    Language lua;
    lua.keywords = words("and break do else elseif end false for function goto if in local nil not or repeat return "
                         "then true until while");
    lua.blockComments = {{QStringLiteral("--[["), QStringLiteral("]]")}};
    lua.lineComments = {QStringLiteral("--")};
    lua.quotes = QStringLiteral("\"'");
    langs.insert(QStringLiteral("lua"), lua);

    Language yaml;
    yaml.keywords = words("true false null yes no on off");
    yaml.lineComments = {QStringLiteral("#")};
    yaml.quotes = QStringLiteral("\"'");
    langs.insert(QStringLiteral("yaml"), yaml);

    Language ini;
    ini.keywords = words("true false");
    ini.lineComments = {QStringLiteral(";"), QStringLiteral("#")};
    ini.quotes = QStringLiteral("\"'");
    langs.insert(QStringLiteral("ini"), ini);

    Language cmake;
    cmake.caseInsensitive = true;
    cmake.keywords = words("if elseif else endif foreach endforeach while endwhile function endfunction macro "
                           "endmacro set unset option project add_executable add_library target_link_libraries "
                           "target_include_directories target_compile_definitions find_package include message "
                           "add_subdirectory install return");
    cmake.lineComments = {QStringLiteral("#")};
    cmake.quotes = QStringLiteral("\"");
    langs.insert(QStringLiteral("cmake"), cmake);

    Language css;
    css.keywords = words("important media import from to");
    css.blockComments = {{QStringLiteral("/*"), QStringLiteral("*/")}};
    css.quotes = QStringLiteral("\"'");
    langs.insert(QStringLiteral("css"), css);

    Language asmLang;
    asmLang.caseInsensitive = true;
    asmLang.keywords = words("ld push pop call ret jp jr djnz inc dec add adc sub sbc and or xor cp nop halt di ei "
                             "mov lea jmp je jne jz jnz cmp test org equ db dw ds defb defw defs include");
    asmLang.lineComments = {QStringLiteral(";")};
    asmLang.quotes = QStringLiteral("\"'");
    langs.insert(QStringLiteral("asm"), asmLang);

    Language xml;
    xml.markup = true;
    langs.insert(QStringLiteral("xml"), xml);

    return langs;
}

const QHash<QString, QString> &aliases()
{
    static const QHash<QString, QString> map = [] {
        QHash<QString, QString> m;
        const auto add = [&m](const char *target, const char *names) {
            for (const QString &n : QString::fromLatin1(names).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                m.insert(n, QString::fromLatin1(target));
            }
        };
        add("cpp", "c cpp c++ cc cxx h hpp hh hxx objc arduino ino");
        add("cs", "cs csharp c#");
        add("java", "java kotlin kt scala groovy");
        add("js", "js javascript jsx ts typescript tsx mjs cjs");
        add("json", "json jsonc json5");
        add("python", "py python python3");
        add("bash", "sh bash zsh shell console fish");
        add("powershell", "ps ps1 psm1 powershell pwsh");
        add("bat", "bat cmd batch dos");
        add("rust", "rs rust");
        add("go", "go golang");
        add("pascal", "pas pascal delphi objectpascal lpr dpr");
        add("sql", "sql mysql postgresql psql sqlite tsql");
        add("lua", "lua");
        add("yaml", "yaml yml");
        add("ini", "ini toml cfg conf properties");
        add("cmake", "cmake");
        add("css", "css scss less");
        add("asm", "asm nasm masm z80 s armasm");
        add("xml", "xml html htm xhtml svg xaml qml vue");
        return m;
    }();
    return map;
}

const Language *findLanguage(const QString &name)
{
    static const QHash<QString, Language> langs = buildLanguages();
    const QString key = aliases().value(name.trimmed().toLower());
    if (key.isEmpty()) {
        return nullptr;
    }
    const auto it = langs.constFind(key);
    return it == langs.constEnd() ? nullptr : &it.value();
}

bool isIdentStart(QChar c, bool dollar)
{
    return c.isLetter() || c == QLatin1Char('_') || (dollar && c == QLatin1Char('$'));
}

bool isIdentPart(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

qsizetype lineEnd(const QString &code, qsizetype from)
{
    const qsizetype nl = code.indexOf(QLatin1Char('\n'), from);
    return nl < 0 ? code.size() : nl;
}

// Markup: comments, tags, attribute names and quoted values.
QList<CodeToken> highlightMarkup(const QString &code)
{
    QList<CodeToken> tokens;
    const qsizetype n = code.size();
    qsizetype i = 0;
    while (i < n) {
        if (code.mid(i, 4) == QLatin1String("<!--")) {
            const qsizetype end = code.indexOf(QLatin1String("-->"), i + 4);
            const qsizetype stop = end < 0 ? n : end + 3;
            tokens.append({i, stop - i, TokenKind::Comment});
            i = stop;
            continue;
        }
        if (code.at(i) != QLatin1Char('<')) {
            ++i;
            continue;
        }
        // Tag name after '<', '</' or '<?'.
        qsizetype j = i + 1;
        if (j < n && (code.at(j) == QLatin1Char('/') || code.at(j) == QLatin1Char('?') || code.at(j) == QLatin1Char('!'))) {
            ++j;
        }
        const qsizetype nameStart = j;
        while (j < n && (isIdentPart(code.at(j)) || code.at(j) == QLatin1Char(':') || code.at(j) == QLatin1Char('-'))) {
            ++j;
        }
        tokens.append({i, j - i, TokenKind::Tag});
        if (j == nameStart) {
            i = j;
            continue;
        }
        // Attributes until '>'.
        while (j < n && code.at(j) != QLatin1Char('>')) {
            const QChar c = code.at(j);
            if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                const qsizetype close = code.indexOf(c, j + 1);
                const qsizetype stop = close < 0 ? n : close + 1;
                tokens.append({j, stop - j, TokenKind::String});
                j = stop;
            } else if (isIdentStart(c, false)) {
                const qsizetype a = j;
                while (j < n && (isIdentPart(code.at(j)) || code.at(j) == QLatin1Char('-') || code.at(j) == QLatin1Char(':'))) {
                    ++j;
                }
                tokens.append({a, j - a, TokenKind::Attribute});
            } else {
                ++j;
            }
        }
        if (j < n) {
            tokens.append({j, 1, TokenKind::Tag});
            ++j;
        }
        i = j;
    }
    return tokens;
}

} // namespace

bool isKnownLanguage(const QString &language)
{
    return findLanguage(language) != nullptr;
}

QList<CodeToken> highlightCode(const QString &code, const QString &language)
{
    const Language *lang = findLanguage(language);
    if (lang == nullptr) {
        return {};
    }
    if (lang->markup) {
        return highlightMarkup(code);
    }
    const Qt::CaseSensitivity cs = lang->caseInsensitive ? Qt::CaseInsensitive : Qt::CaseSensitive;
    QList<CodeToken> tokens;
    const qsizetype n = code.size();
    qsizetype i = 0;
    // Every branch advances i by at least one character, so the scan ends after at most n passes.
    while (i < n) {
        const QChar c = code.at(i);

        bool matched = false;
        for (const auto &bc : lang->blockComments) {
            if (QStringView(code).mid(i).startsWith(bc.first, cs)) {
                const qsizetype end = code.indexOf(bc.second, i + bc.first.size(), cs);
                const qsizetype stop = end < 0 ? n : end + bc.second.size();
                tokens.append({i, stop - i, TokenKind::Comment});
                i = stop;
                matched = true;
                break;
            }
        }
        if (matched) {
            continue;
        }
        for (const QString &lc : lang->lineComments) {
            const bool atWordStart = i == 0 || !isIdentPart(code.at(i - 1));
            if (atWordStart && QStringView(code).mid(i).startsWith(lc, cs)) {
                const qsizetype stop = lineEnd(code, i);
                tokens.append({i, stop - i, TokenKind::Comment});
                i = stop;
                matched = true;
                break;
            }
        }
        if (matched) {
            continue;
        }
        if (lang->hashPreprocessor && c == QLatin1Char('#')) {
            // lastIndexOf with a negative start searches from the end, so guard i == 0.
            const qsizetype lineStart = i == 0 ? 0 : code.lastIndexOf(QLatin1Char('\n'), i - 1) + 1;
            if (QStringView(code).mid(lineStart, i - lineStart).trimmed().isEmpty()) {
                const qsizetype stop = lineEnd(code, i);
                tokens.append({i, stop - i, TokenKind::Preprocessor});
                i = stop;
                continue;
            }
        }
        if (lang->tripleQuotes && (code.mid(i, 3) == QLatin1String("\"\"\"") || code.mid(i, 3) == QLatin1String("'''"))) {
            const QString delim = code.mid(i, 3);
            const qsizetype end = code.indexOf(delim, i + 3);
            const qsizetype stop = end < 0 ? n : end + 3;
            tokens.append({i, stop - i, TokenKind::String});
            i = stop;
            continue;
        }
        if (lang->quotes.contains(c)) {
            qsizetype j = i + 1;
            while (j < n && code.at(j) != c) {
                if (code.at(j) == QLatin1Char('\\') && j + 1 < n) {
                    j += 2;
                    continue;
                }
                if (code.at(j) == QLatin1Char('\n') && c != QLatin1Char('`')) {
                    break;
                }
                ++j;
            }
            const qsizetype stop = std::min(j + 1, n);
            tokens.append({i, stop - i, TokenKind::String});
            i = stop;
            continue;
        }
        if (c.isDigit() && (i == 0 || !isIdentPart(code.at(i - 1)))) {
            qsizetype j = i + 1;
            while (j < n && (code.at(j).isLetterOrNumber() || code.at(j) == QLatin1Char('.') || code.at(j) == QLatin1Char('_'))) {
                ++j;
            }
            tokens.append({i, j - i, TokenKind::Number});
            i = j;
            continue;
        }
        const bool dashWord = lang->caseInsensitive && c == QLatin1Char('-') && i + 1 < n && code.at(i + 1).isLetter();
        if (isIdentStart(c, lang->dollarIdentifiers) || dashWord) {
            qsizetype j = i + 1;
            while (j < n && (isIdentPart(code.at(j)) || (lang->caseInsensitive && code.at(j) == QLatin1Char('-')))) {
                ++j;
            }
            const QString word = code.mid(i, j - i);
            const QString key = lang->caseInsensitive ? word.toLower() : word;
            if (c == QLatin1Char('$')) {
                tokens.append({i, j - i, TokenKind::Type});
            } else if (lang->keywords.contains(key)) {
                tokens.append({i, j - i, TokenKind::Keyword});
            } else if (lang->types.contains(key)) {
                tokens.append({i, j - i, TokenKind::Type});
            }
            i = j;
            continue;
        }
        ++i;
    }
    return tokens;
}

} // namespace dcpp
