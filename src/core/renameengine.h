#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace bifrons {

// DC-style masks: [N] [E] (ranges [N2-5] [N3-] [N4]), [C], [P], [Y][M][D][h][n][s], "[[" for '['.
struct RenameOptions
{
    enum class Case
    {
        Unchanged,
        Lower,
        Upper,
        FirstUpper,
        EachWord
    };

    QString nameMask = QStringLiteral("[N]");
    QString extMask = QStringLiteral("[E]");
    QString find;
    QString replace;
    bool regex = false;
    bool findCaseSensitive = false;
    Case caseMode = Case::Unchanged;
    int counterStart = 1;
    int counterStep = 1;
    int counterWidth = 1;
};

struct RenameItem
{
    QString path;
    bool isDir = false;
    QDateTime modified;
};

struct RenamePreview
{
    QStringList newNames;
    // One entry per item: empty when that item is fine.
    QStringList problems;
    // Set when the masks or the expression themselves are invalid.
    QString error;

    bool ok() const;
};

// One completed rename, as full paths.
struct RenameStep
{
    QString from;
    QString to;
};

struct RenameResult
{
    bool ok = false;
    QList<RenameStep> done;
    QString error;
};

// Computes new names and checks the whole batch (bad characters, duplicates, existing files).
RenamePreview previewRenames(const QList<RenameItem> &items, const RenameOptions &options);

// Two-step rename via temporary names (swaps work). Rolls back fully on failure.
RenameResult applyRenames(const QList<RenameItem> &items, const QStringList &newNames);

// Reverses a previous result's steps, with the same rollback guarantee.
RenameResult undoRenames(const QList<RenameStep> &done);

} // namespace bifrons
