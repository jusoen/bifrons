#include "core/renameengine.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace bifrons {

namespace {

constexpr int MaxTempTries = 1000;

Qt::CaseSensitivity pathCase()
{
#if defined(Q_OS_WIN)
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QString pathKey(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    return pathCase() == Qt::CaseInsensitive ? clean.toCaseFolded() : clean;
}

struct NameParts
{
    QString name;
    QString ext;
};

NameParts split(const QString &fileName, bool isDir)
{
    const qsizetype dot = isDir ? -1 : fileName.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == fileName.size() - 1) {
        return {fileName, QString()};
    }
    return {fileName.left(dot), fileName.mid(dot + 1)};
}

// Characters [a-b], [a-] or [a] of text, 1-based and clamped to the text.
QString range(const QString &text, const QString &spec, bool *ok)
{
    *ok = true;
    if (spec.isEmpty()) {
        return text;
    }
    const qsizetype dash = spec.indexOf(QLatin1Char('-'));
    bool okA = false;
    const int a = (dash < 0 ? spec : spec.left(dash)).toInt(&okA);
    int b = a;
    bool okB = true;
    if (dash >= 0) {
        const QString tail = spec.mid(dash + 1);
        b = tail.isEmpty() ? static_cast<int>(text.size()) : tail.toInt(&okB);
    }
    if (!okA || !okB || a < 1 || b < a) {
        *ok = false;
        return QString();
    }
    return text.mid(a - 1, b - a + 1);
}

// Expands one mask. Returns false with error set for a malformed mask.
bool expandMask(const QString &mask, const RenameItem &item, const NameParts &parts, int counter,
                const RenameOptions &options, QString *out, QString *error)
{
    out->clear();
    qsizetype i = 0;
    while (i < mask.size()) {
        const QChar ch = mask.at(i);
        if (ch != QLatin1Char('[')) {
            out->append(ch);
            ++i;
            continue;
        }
        if (i + 1 < mask.size() && mask.at(i + 1) == QLatin1Char('[')) {
            out->append(QLatin1Char('['));
            i += 2;
            continue;
        }
        const qsizetype close = mask.indexOf(QLatin1Char(']'), i + 1);
        if (close < 0) {
            *error = QStringLiteral("Unclosed '[' in mask \"%1\".").arg(mask);
            return false;
        }
        const QString token = mask.mid(i + 1, close - i - 1);
        i = close + 1;
        if (token.isEmpty()) {
            *error = QStringLiteral("Empty placeholder [] in mask \"%1\".").arg(mask);
            return false;
        }
        const QChar kind = token.at(0);
        const QString spec = token.mid(1);
        bool ok = true;
        const QDateTime &m = item.modified;
        if (kind == QLatin1Char('N')) {
            out->append(range(parts.name, spec, &ok));
        } else if (kind == QLatin1Char('E')) {
            out->append(range(parts.ext, spec, &ok));
        } else if (spec.isEmpty() && kind == QLatin1Char('C')) {
            out->append(QStringLiteral("%1").arg(counter, options.counterWidth, 10, QLatin1Char('0')));
        } else if (spec.isEmpty() && kind == QLatin1Char('P')) {
            out->append(QFileInfo(QFileInfo(item.path).absolutePath()).fileName());
        } else if (spec.isEmpty() && kind == QLatin1Char('Y')) {
            out->append(m.toString(QStringLiteral("yyyy")));
        } else if (spec.isEmpty() && kind == QLatin1Char('M')) {
            out->append(m.toString(QStringLiteral("MM")));
        } else if (spec.isEmpty() && kind == QLatin1Char('D')) {
            out->append(m.toString(QStringLiteral("dd")));
        } else if (spec.isEmpty() && kind == QLatin1Char('h')) {
            out->append(m.toString(QStringLiteral("HH")));
        } else if (spec.isEmpty() && kind == QLatin1Char('n')) {
            out->append(m.toString(QStringLiteral("mm")));
        } else if (spec.isEmpty() && kind == QLatin1Char('s')) {
            out->append(m.toString(QStringLiteral("ss")));
        } else {
            ok = false;
        }
        if (!ok) {
            *error = QStringLiteral("Unknown placeholder [%1].").arg(token);
            return false;
        }
    }
    return true;
}

QString applyCase(const QString &name, RenameOptions::Case mode)
{
    switch (mode) {
    case RenameOptions::Case::Lower:
        return name.toLower();
    case RenameOptions::Case::Upper:
        return name.toUpper();
    case RenameOptions::Case::FirstUpper:
        return name.isEmpty() ? name : name.left(1).toUpper() + name.mid(1).toLower();
    case RenameOptions::Case::EachWord: {
        QString out = name.toLower();
        bool startOfWord = true;
        for (QChar &c : out) {
            if (c.isLetterOrNumber()) {
                if (startOfWord) {
                    c = c.toUpper();
                }
                startOfWord = false;
            } else {
                startOfWord = true;
            }
        }
        return out;
    }
    case RenameOptions::Case::Unchanged:
        break;
    }
    return name;
}

QString invalidCharacterIn(const QString &name)
{
#if defined(Q_OS_WIN)
    static const QString bad = QStringLiteral("\\/:*?\"<>|");
#else
    static const QString bad = QStringLiteral("/");
#endif
    for (const QChar c : name) {
        if (bad.contains(c) || c.unicode() < 32) {
            return QString(c);
        }
    }
    return QString();
}

// Renames from -> to, never overwriting. QDir::rename does not fall back to copying.
bool renameNoOverwrite(const QString &from, const QString &to)
{
    return QDir().rename(from, to);
}

// Undoes steps newest first. Returns false if any could not be undone.
bool rollBack(const QList<RenameStep> &steps)
{
    bool all = true;
    for (auto it = steps.crbegin(); it != steps.crend(); ++it) {
        if (!renameNoOverwrite(it->to, it->from)) {
            all = false;
        }
    }
    return all;
}

QString tempNameFor(const QString &path, int serial)
{
    const QFileInfo info(path);
    for (int n = 0; n < MaxTempTries; ++n) {
        const QString candidate = QDir(info.absolutePath())
                                      .filePath(QStringLiteral(".bifrons-mr-%1-%2").arg(serial).arg(n));
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

RenameResult runSteps(const QList<RenameStep> &steps)
{
    RenameResult result;
    // Phase 1: every source to a unique temporary name in its own folder.
    QList<RenameStep> phase1;
    for (int i = 0; i < steps.size(); ++i) {
        const QString temp = tempNameFor(steps.at(i).from, i);
        if (temp.isEmpty() || !renameNoOverwrite(steps.at(i).from, temp)) {
            const bool restored = rollBack(phase1);
            result.error = QStringLiteral("Cannot rename %1.%2")
                               .arg(QDir::toNativeSeparators(steps.at(i).from),
                                    restored ? QString() : QStringLiteral(" Some names could not be restored."));
            return result;
        }
        phase1.append({steps.at(i).from, temp});
    }
    // Phase 2: temporary names to the final names.
    QList<RenameStep> phase2;
    for (int i = 0; i < steps.size(); ++i) {
        if (!renameNoOverwrite(phase1.at(i).to, steps.at(i).to)) {
            const bool restored = rollBack(phase2) && rollBack(phase1);
            result.error = QStringLiteral("Cannot rename %1 to %2.%3")
                               .arg(QDir::toNativeSeparators(steps.at(i).from),
                                    QFileInfo(steps.at(i).to).fileName(),
                                    restored ? QString() : QStringLiteral(" Some names could not be restored."));
            return result;
        }
        phase2.append({phase1.at(i).to, steps.at(i).to});
    }
    result.ok = true;
    result.done = steps;
    return result;
}

} // namespace

bool RenamePreview::ok() const
{
    if (!error.isEmpty()) {
        return false;
    }
    for (const QString &p : problems) {
        if (!p.isEmpty()) {
            return false;
        }
    }
    return true;
}

RenamePreview previewRenames(const QList<RenameItem> &items, const RenameOptions &options)
{
    RenamePreview preview;
    QRegularExpression re;
    if (!options.find.isEmpty() && options.regex) {
        re = QRegularExpression(options.find, options.findCaseSensitive
                                                  ? QRegularExpression::NoPatternOption
                                                  : QRegularExpression::CaseInsensitiveOption);
        if (!re.isValid()) {
            preview.error = QStringLiteral("Invalid expression: %1").arg(re.errorString());
            return preview;
        }
    }

    for (int i = 0; i < items.size(); ++i) {
        const RenameItem &item = items.at(i);
        const NameParts parts = split(QFileInfo(item.path).fileName(), item.isDir);
        const int counter = options.counterStart + i * options.counterStep;
        QString name;
        QString ext;
        if (!expandMask(options.nameMask, item, parts, counter, options, &name, &preview.error)
            || !expandMask(options.extMask, item, parts, counter, options, &ext, &preview.error)) {
            preview.newNames.clear();
            preview.problems.clear();
            return preview;
        }
        QString full = ext.isEmpty() ? name : name + QLatin1Char('.') + ext;
        if (!options.find.isEmpty()) {
            if (options.regex) {
                full.replace(re, options.replace);
            } else {
                full.replace(options.find, options.replace,
                             options.findCaseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
            }
        }
        preview.newNames.append(applyCase(full, options.caseMode));
    }

    // Batch checks: paths being vacated by this batch may be reused by it.
    QSet<QString> vacated;
    for (const RenameItem &item : items) {
        vacated.insert(pathKey(item.path));
    }
    QHash<QString, int> seen;
    for (int i = 0; i < items.size(); ++i) {
        const QString &newName = preview.newNames.at(i);
        const QString target = QDir(QFileInfo(items.at(i).path).absolutePath()).filePath(newName);
        QString problem;
        const QString bad = invalidCharacterIn(newName);
        if (newName.trimmed().isEmpty() || newName == QLatin1String(".") || newName == QLatin1String("..")) {
            problem = QStringLiteral("Empty or invalid name.");
        } else if (!bad.isEmpty()) {
            problem = QStringLiteral("Contains '%1'.").arg(bad);
        } else if (seen.contains(pathKey(target))) {
            problem = QStringLiteral("Same new name as item %1.").arg(seen.value(pathKey(target)) + 1);
        } else if (QFileInfo::exists(target) && !vacated.contains(pathKey(target))) {
            problem = QStringLiteral("A file with this name already exists.");
        }
        seen.insert(pathKey(target), i);
        preview.problems.append(problem);
    }
    return preview;
}

RenameResult applyRenames(const QList<RenameItem> &items, const QStringList &newNames)
{
    if (items.size() != newNames.size()) {
        return {false, {}, QStringLiteral("Internal error: name count mismatch.")};
    }
    QList<RenameStep> steps;
    for (int i = 0; i < items.size(); ++i) {
        const QString target = QDir(QFileInfo(items.at(i).path).absolutePath()).filePath(newNames.at(i));
        // Exact comparison, so a case-only change is still applied.
        if (QDir::cleanPath(target) != QDir::cleanPath(items.at(i).path)) {
            steps.append({items.at(i).path, target});
        }
    }
    return runSteps(steps);
}

RenameResult undoRenames(const QList<RenameStep> &done)
{
    QList<RenameStep> reverse;
    for (const RenameStep &step : done) {
        reverse.append({step.to, step.from});
    }
    return runSteps(reverse);
}

} // namespace bifrons
