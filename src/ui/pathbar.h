#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QStackedLayout;
class QToolButton;

namespace bifrons {

// One clickable segment of the breadcrumb trail.
struct Crumb
{
    QString label;
    // Folder to open on click. Empty makes the segment plain text.
    QString path;
};

// Breadcrumb trail of the current folder. Clicking the empty space after the
// last segment swaps in a line edit for typing a path. Escape or leaving the
// edit swaps the trail back.
class PathBar : public QWidget
{
    Q_OBJECT

public:
    explicit PathBar(QWidget *parent = nullptr);

    // editText is what the line edit shows when editing starts.
    void setLocation(const QString &editText, const QList<Crumb> &crumbs);
    void setActive(bool active);
    void startEditing();
    QLineEdit *lineEdit() const;

signals:
    // A segment was clicked (its path) or a typed path was confirmed (the raw text).
    void pathRequested(const QString &path);
    // Editing was cancelled with Escape.
    void editingCancelled();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Segment
    {
        QToolButton *button = nullptr;
        // Separator drawn before the button, nullptr on the first segment.
        QLabel *separator = nullptr;
        QString path;
    };

    void rebuild();
    void updateElision();
    void stopEditing();
    void applyStyle();
    QLabel *makeSeparator();

    QStackedLayout *m_stack = nullptr;
    QFrame *m_crumbFrame = nullptr;
    QHBoxLayout *m_crumbLayout = nullptr;
    QLineEdit *m_edit = nullptr;
    QToolButton *m_overflow = nullptr;
    QLabel *m_overflowSeparator = nullptr;

    QList<Crumb> m_crumbs;
    QList<Segment> m_segments;
    QString m_editText;
    bool m_active = false;
};

} // namespace bifrons
