#pragma once

#include <QTextBrowser>

namespace dcpp {

// Qt's GitHub-flavoured markdown, restyled GitHub-like with highlighted code blocks.
class MarkdownView : public QTextBrowser
{
    Q_OBJECT

public:
    explicit MarkdownView(QWidget *parent = nullptr);

    // baseDir resolves relative images and links.
    void showMarkdown(const QString &markdown, const QString &baseDir);

signals:
    void markdownLinkActivated(const QString &path);

private:
    void onAnchorClicked(const QUrl &url);
    void styleDocument();

    QString m_baseDir;
};

} // namespace dcpp
