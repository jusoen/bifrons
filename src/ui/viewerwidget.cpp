#include "ui/viewerwidget.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <QSyntaxHighlighter>

#include <algorithm>

#include "core/codehighlight.h"
#include "core/textsniff.h"
#include "ui/codetheme.h"
#include "ui/markdownview.h"

namespace bifrons {

namespace {

constexpr qint64 MaxHexBytes = 1024 * 1024;
constexpr int HexBytesPerLine = 16;

bool readHead(const QString &path, qint64 maxBytes, QByteArray *data, bool *truncated, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = f.errorString();
        return false;
    }
    *data = f.read(maxBytes);
    if (data->isEmpty() && f.error() != QFileDevice::NoError) {
        *error = f.errorString();
        return false;
    }
    *truncated = f.size() > data->size();
    return true;
}

bool isMarkdown(const QString &path)
{
    static const QStringList exts{QStringLiteral("md"), QStringLiteral("markdown"), QStringLiteral("mdown"),
                                  QStringLiteral("mkd")};
    return exts.contains(QFileInfo(path).suffix().toLower());
}

constexpr qsizetype MaxHighlightChars = 4 * 1024 * 1024;

} // namespace

// Paints precomputed whole-file tokens line by line, so multi-line comments and strings work.
class TokenHighlighter : public QSyntaxHighlighter
{
public:
    using QSyntaxHighlighter::QSyntaxHighlighter;

    // Takes effect on the next setPlainText. Tokens must be sorted and non-overlapping.
    void setTokens(QList<CodeToken> tokens)
    {
        m_tokens = std::move(tokens);
        const CodeTheme theme = CodeTheme::current();
        for (int k = 0; k <= static_cast<int>(TokenKind::Attribute); ++k) {
            QTextCharFormat f;
            f.setForeground(theme.colourFor(static_cast<TokenKind>(k)));
            f.setFontItalic(static_cast<TokenKind>(k) == TokenKind::Comment);
            m_formats[k] = f;
        }
    }

protected:
    void highlightBlock(const QString &text) override
    {
        if (m_tokens.isEmpty()) {
            return;
        }
        const qsizetype blockStart = currentBlock().position();
        const qsizetype blockEnd = blockStart + text.size();
        auto it = std::lower_bound(m_tokens.cbegin(), m_tokens.cend(), blockStart,
                                   [](const CodeToken &t, qsizetype pos) { return t.start + t.length <= pos; });
        for (; it != m_tokens.cend() && it->start < blockEnd; ++it) {
            const qsizetype from = std::max(it->start, blockStart);
            const qsizetype to = std::min(it->start + it->length, blockEnd);
            setFormat(static_cast<int>(from - blockStart), static_cast<int>(to - from),
                      m_formats[static_cast<int>(it->kind)]);
        }
    }

private:
    QList<CodeToken> m_tokens;
    QTextCharFormat m_formats[static_cast<int>(TokenKind::Attribute) + 1];
};

// Draws an image scaled down to fit, never enlarged, centred.
class ImageView : public QWidget
{
public:
    using QWidget::QWidget;

    void setImage(const QImage &image)
    {
        m_image = image;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_image.isNull()) {
            return;
        }
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QSize size = m_image.size();
        if (size.width() > width() || size.height() > height()) {
            size.scale(this->size(), Qt::KeepAspectRatio);
        }
        const QRect target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
        p.drawImage(target, m_image);
    }

private:
    QImage m_image;
};

ViewerWidget::ViewerWidget(qint64 maxBytes, QWidget *parent)
    : QWidget(parent)
    , m_maxBytes(maxBytes)
{
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    m_message = new QLabel(this);
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    m_image = new ImageView(this);
    m_image->setFocusPolicy(Qt::StrongFocus);
    m_markdown = new MarkdownView(this);
    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setFont(mono);
    m_highlighter = new TokenHighlighter(m_text->document());
    m_hex = new QPlainTextEdit(this);
    m_hex->setReadOnly(true);
    m_hex->setFont(mono);
    m_hex->setLineWrapMode(QPlainTextEdit::NoWrap);
    setWrap(false);

    m_stack = new QStackedWidget(this);
    for (QWidget *page : {static_cast<QWidget *>(m_message), static_cast<QWidget *>(m_image),
                          static_cast<QWidget *>(m_markdown), static_cast<QWidget *>(m_text),
                          static_cast<QWidget *>(m_hex)}) {
        m_stack->addWidget(page);
        page->installEventFilter(this);
    }
    m_status = new QLabel(this);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(m_stack, 1);
    layout->addWidget(m_status);

    connect(m_markdown, &MarkdownView::markdownLinkActivated, this, &ViewerWidget::markdownLinkActivated);
    showFile(QString());
}

QString ViewerWidget::path() const
{
    return m_path;
}

void ViewerWidget::setMode(Mode mode)
{
    if (mode != m_mode) {
        m_mode = mode;
        showFile(m_path);
    }
}

ViewerWidget::Mode ViewerWidget::mode() const
{
    return m_mode;
}

void ViewerWidget::setWrap(bool wrap)
{
    m_wrap = wrap;
    m_text->setLineWrapMode(wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
}

bool ViewerWidget::wrap() const
{
    return m_wrap;
}

bool ViewerWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Tab && key->modifiers() == Qt::NoModifier) {
            emit tabPressed();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ViewerWidget::setPage(QWidget *page, const QString &status)
{
    const bool hadFocus = m_stack->currentWidget() != nullptr && m_stack->currentWidget()->hasFocus();
    m_stack->setCurrentWidget(page);
    setFocusProxy(page);
    if (hadFocus) {
        page->setFocus();
    }
    m_status->setText(status);
}

void ViewerWidget::showMessage(const QString &message)
{
    m_message->setText(message);
    setPage(m_message, QString());
}

void ViewerWidget::showFile(const QString &path)
{
    m_path = path;
    if (path.isEmpty()) {
        showMessage(QString());
        return;
    }
    const QFileInfo info(path);
    const QLocale locale;
    if (info.isDir()) {
        const qsizetype n = QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).size();
        showMessage(tr("Folder %1\n%2 items").arg(info.fileName()).arg(n));
        return;
    }
    if (!info.exists()) {
        showMessage(tr("%1 does not exist.").arg(QDir::toNativeSeparators(path)));
        return;
    }
    const QString size = locale.formattedDataSize(info.size());

    if (m_mode == Mode::Auto) {
        const QByteArray format = QImageReader::imageFormat(path);
        if (!format.isEmpty()) {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            const QImage image = reader.read();
            if (!image.isNull()) {
                m_image->setImage(image);
                setPage(m_image, tr("%1 x %2  %3  %4")
                                     .arg(image.width())
                                     .arg(image.height())
                                     .arg(QString::fromLatin1(format).toUpper(), size));
                return;
            }
        }
    }

    QByteArray bytes;
    bool truncated = false;
    QString error;
    if (!readHead(path, m_mode == Mode::Hex ? std::min(m_maxBytes, MaxHexBytes) : m_maxBytes, &bytes, &truncated,
                  &error)) {
        showMessage(tr("Cannot read %1\n%2").arg(info.fileName(), error));
        return;
    }

    if (m_mode == Mode::Hex) {
        showHex(bytes, truncated);
        return;
    }
    if (m_mode == Mode::Auto && isMarkdown(path)) {
        QString encoding;
        m_markdown->showMarkdown(decodeText(bytes, &encoding, truncated), info.absolutePath());
        setPage(m_markdown, tr("Markdown  %1  %2%3").arg(encoding, size, truncated ? tr("  (truncated)") : QString()));
        return;
    }
    if (m_mode == Mode::Text || looksLikeText(bytes)) {
        showText(bytes, truncated, size);
        return;
    }
    showHex(bytes.left(MaxHexBytes), truncated || bytes.size() > MaxHexBytes);
}

void ViewerWidget::showText(const QByteArray &bytes, bool truncated, const QString &kind)
{
    QString encoding;
    QString text = decodeText(bytes, &encoding, truncated);
    // One newline per line, so token offsets line up with document positions.
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    const QString language = text.size() <= MaxHighlightChars ? languageForFile(m_path) : QString();
    m_highlighter->setTokens(language.isEmpty() ? QList<CodeToken>{} : highlightCode(text, language));
    m_text->setPlainText(text);
    const QString label = language.isEmpty() ? tr("Text") : tr("Text (%1)").arg(language);
    setPage(m_text, tr("%1  %2  %3  %4 lines%5")
                        .arg(label, encoding, kind)
                        .arg(m_text->blockCount())
                        .arg(truncated ? tr("  (first %1 shown)").arg(QLocale().formattedDataSize(bytes.size()))
                                       : QString()));
}

void ViewerWidget::showHex(const QByteArray &bytes, bool truncated)
{
    QString out;
    out.reserve(bytes.size() / HexBytesPerLine * 80 + 80);
    for (qsizetype offset = 0; offset < bytes.size(); offset += HexBytesPerLine) {
        const QByteArray line = bytes.mid(offset, HexBytesPerLine);
        out += QStringLiteral("%1  ").arg(offset, 8, 16, QLatin1Char('0'));
        for (int i = 0; i < HexBytesPerLine; ++i) {
            if (i < line.size()) {
                out += QStringLiteral("%1 ").arg(static_cast<unsigned char>(line.at(i)), 2, 16, QLatin1Char('0'));
            } else {
                out += QStringLiteral("   ");
            }
            if (i == 7) {
                out += QLatin1Char(' ');
            }
        }
        out += QLatin1Char(' ');
        for (const char ch : line) {
            const auto b = static_cast<unsigned char>(ch);
            out += (b >= 0x20 && b < 0x7f) ? QLatin1Char(static_cast<char>(b)) : QLatin1Char('.');
        }
        out += QLatin1Char('\n');
    }
    m_hex->setPlainText(out);
    setPage(m_hex, tr("Hex  %1%2")
                       .arg(QLocale().formattedDataSize(bytes.size()))
                       .arg(truncated ? tr("  (start of file only)") : QString()));
}

} // namespace bifrons
