#pragma once

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QStackedWidget;

namespace bifrons {

class ImageView;
class MarkdownView;
class TokenHighlighter;

// Shows one file as an image, rendered markdown, text or hex. Used by F3 and quick view.
class ViewerWidget : public QWidget
{
    Q_OBJECT

public:
    enum class Mode
    {
        Auto,
        Text,
        Hex
    };

    // maxBytes caps how much of a text or binary file is read.
    explicit ViewerWidget(qint64 maxBytes, QWidget *parent = nullptr);

    // An empty path clears the view.
    void showFile(const QString &path);
    QString path() const;

    void setMode(Mode mode);
    Mode mode() const;
    void setWrap(bool wrap);
    bool wrap() const;

signals:
    void markdownLinkActivated(const QString &path);
    // Tab pressed inside the viewer.
    void tabPressed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void showMessage(const QString &message);
    void showText(const QByteArray &data, bool truncated, const QString &kind);
    void showHex(const QByteArray &data, bool truncated);
    void setPage(QWidget *page, const QString &status);

    qint64 m_maxBytes;
    QString m_path;
    Mode m_mode = Mode::Auto;
    bool m_wrap = false;

    QStackedWidget *m_stack = nullptr;
    QLabel *m_message = nullptr;
    ImageView *m_image = nullptr;
    MarkdownView *m_markdown = nullptr;
    QPlainTextEdit *m_text = nullptr;
    TokenHighlighter *m_highlighter = nullptr;
    QPlainTextEdit *m_hex = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace bifrons
