#include "ui/markdownview.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>

#include "core/codehighlight.h"
#include "core/markdownprep.h"
#include "ui/codetheme.h"

#include <algorithm>

namespace bifrons {

namespace {

using Theme = CodeTheme;

Theme currentTheme()
{
    return CodeTheme::current();
}

QColor tokenColour(const Theme &t, TokenKind kind)
{
    return t.colourFor(kind);
}

bool isCodeBlock(const QTextBlock &block)
{
    const QTextBlockFormat f = block.blockFormat();
    return f.hasProperty(QTextFormat::BlockCodeFence) || f.hasProperty(QTextFormat::BlockCodeLanguage);
}

void styleTables(QTextFrame *frame, const Theme &t)
{
    for (QTextFrame *child : frame->childFrames()) {
        if (auto *table = qobject_cast<QTextTable *>(child)) {
            QTextTableFormat f = table->format();
            f.setBorder(1);
            f.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
            f.setBorderBrush(t.border);
            f.setBorderCollapse(true);
            f.setCellSpacing(0);
            f.setCellPadding(6);
            f.setTopMargin(8);
            f.setBottomMargin(8);
            table->setFormat(f);
            for (int col = 0; col < table->columns(); ++col) {
                QTextTableCell cell = table->cellAt(0, col);
                QTextCharFormat cf = cell.format();
                cf.setBackground(t.codeBg);
                cell.setFormat(cf);
                QTextCursor c = cell.firstCursorPosition();
                c.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
                QTextCharFormat bold;
                bold.setFontWeight(QFont::DemiBold);
                c.mergeCharFormat(bold);
            }
        }
        styleTables(child, t);
    }
}

} // namespace

MarkdownView::MarkdownView(QWidget *parent)
    : QTextBrowser(parent)
{
    setOpenLinks(false);
    setFrameShape(QFrame::NoFrame);
    connect(this, &QTextBrowser::anchorClicked, this, &MarkdownView::onAnchorClicked);
}

void MarkdownView::showMarkdown(const QString &markdown, const QString &baseDir)
{
    m_baseDir = baseDir;
    setSearchPaths({baseDir});

    const Theme t = currentTheme();
    QPalette pal = palette();
    pal.setColor(QPalette::Base, t.bg);
    pal.setColor(QPalette::Text, t.fg);
    setPalette(pal);

    QTextDocument *doc = document();
    doc->setUndoRedoEnabled(false);
    QFont body = font();
    body.setPointSizeF(11.0);
    doc->setDefaultFont(body);
    doc->setDocumentMargin(24);
    doc->setMarkdown(applyLabelBreaks(markdown), QTextDocument::MarkdownDialectGitHub);
    styleDocument();
    moveCursor(QTextCursor::Start);
}

void MarkdownView::styleDocument()
{
    QTextDocument *doc = document();
    const Theme t = currentTheme();
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    struct Run
    {
        int from = 0;
        int to = 0;
        QString language;
    };
    QList<Run> codeRuns;
    QList<QPair<int, int>> inlineCode;
    QList<QPair<int, int>> links;
    struct Heading
    {
        int from = 0;
        int to = 0;
        int level = 0;
        QString slug;
    };
    QList<Heading> headings;
    QList<QPair<int, int>> quotes;

    // Collect first, then format, so formatting never invalidates the walk.
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QTextBlockFormat bf = b.blockFormat();
        const int from = b.position();
        const int to = b.position() + b.length() - 1;
        if (isCodeBlock(b)) {
            const QString lang = bf.stringProperty(QTextFormat::BlockCodeLanguage);
            if (!codeRuns.isEmpty() && codeRuns.last().to + 1 == from && codeRuns.last().language == lang) {
                codeRuns.last().to = to;
            } else {
                codeRuns.append({from, to, lang});
            }
            continue;
        }
        if (bf.headingLevel() > 0) {
            headings.append({from, to, bf.headingLevel(), headingSlug(b.text())});
        }
        if (bf.hasProperty(QTextFormat::BlockQuoteLevel)) {
            quotes.append({from, to});
        }
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid()) {
                continue;
            }
            const QTextCharFormat cf = frag.charFormat();
            const int fs = frag.position();
            const int fe = frag.position() + frag.length();
            if (cf.fontFixedPitch()) {
                inlineCode.append({fs, fe});
            }
            if (cf.isAnchor() && !cf.anchorHref().isEmpty()) {
                links.append({fs, fe});
            }
        }
    }

    QTextCursor c(doc);
    c.beginEditBlock();
    const auto select = [&c](int from, int to) {
        c.setPosition(from);
        c.setPosition(to, QTextCursor::KeepAnchor);
    };

    for (const Heading &h : headings) {
        static const qreal scale[] = {2.0, 1.5, 1.25, 1.0, 0.875, 0.85};
        select(h.from, h.to);
        QTextCharFormat cf;
        cf.setFontPointSize(11.0 * scale[std::clamp(h.level, 1, 6) - 1]);
        cf.setFontWeight(QFont::DemiBold);
        cf.setForeground(h.level >= 6 ? t.muted : t.fg);
        if (!h.slug.isEmpty()) {
            cf.setAnchor(true);
            cf.setAnchorNames({h.slug});
        }
        c.mergeCharFormat(cf);
        QTextBlockFormat bf;
        bf.setTopMargin(h.level <= 2 ? 20 : 14);
        bf.setBottomMargin(8);
        c.mergeBlockFormat(bf);
    }

    for (const auto &q : quotes) {
        select(q.first, q.second);
        QTextCharFormat cf;
        cf.setForeground(t.muted);
        c.mergeCharFormat(cf);
    }

    for (const auto &l : links) {
        select(l.first, l.second);
        QTextCharFormat cf;
        cf.setForeground(t.link);
        c.mergeCharFormat(cf);
    }

    for (const auto &ic : inlineCode) {
        select(ic.first, ic.second);
        QTextCharFormat cf;
        cf.setFontFamilies({mono.family()});
        cf.setBackground(t.codeBg);
        c.mergeCharFormat(cf);
    }

    for (const Run &run : codeRuns) {
        select(run.from, run.to);
        QTextBlockFormat bf;
        bf.setBackground(t.codeBg);
        bf.setLeftMargin(4);
        bf.setNonBreakableLines(true);
        c.mergeBlockFormat(bf);
        QTextCharFormat base;
        base.setFontFamilies({mono.family()});
        base.setFontPointSize(10.0);
        base.setForeground(t.fg);
        base.clearBackground();
        c.mergeCharFormat(base);

        // Blocks in a run are adjacent, so run text offsets map straight onto document positions.
        select(run.from, run.to);
        const QString text = c.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
        for (const CodeToken &tok : highlightCode(text, run.language)) {
            select(run.from + static_cast<int>(tok.start), run.from + static_cast<int>(tok.start + tok.length));
            QTextCharFormat cf;
            cf.setForeground(tokenColour(t, tok.kind));
            if (tok.kind == TokenKind::Comment) {
                cf.setFontItalic(true);
            }
            c.mergeCharFormat(cf);
        }
    }

    styleTables(doc->rootFrame(), t);
    c.endEditBlock();
}

void MarkdownView::onAnchorClicked(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https") || scheme == QLatin1String("mailto")
        || scheme == QLatin1String("ftp")) {
        // Failure to open just leaves the preview where it is.
        (void)QDesktopServices::openUrl(url);
        return;
    }
    if (url.path().isEmpty() && url.hasFragment()) {
        scrollToAnchor(url.fragment());
        return;
    }
    const QString path = url.isLocalFile() ? url.toLocalFile() : QDir(m_baseDir).filePath(url.path());
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("md") || suffix == QLatin1String("markdown")) {
        emit markdownLinkActivated(QDir::cleanPath(path));
        return;
    }
    // Same as above: an unopenable file leaves the preview unchanged.
    (void)QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

} // namespace bifrons
