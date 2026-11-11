#include "ui/pathbar.h"

#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QStackedLayout>
#include <QToolButton>

namespace bifrons {

namespace {

// Empty space kept after the last segment so there is always somewhere to click to edit.
constexpr int EditClickReservePx = 24;
// Room either side of a segment's text for the hover highlight.
constexpr int SegmentPaddingPx = 2;

// The platform style gives tool buttons a minimum width, which spreads short names apart.
void fitToText(QToolButton *button)
{
    const int text = button->fontMetrics().horizontalAdvance(button->text());
    button->setFixedWidth(text + 2 * SegmentPaddingPx);
}

} // namespace

PathBar::PathBar(QWidget *parent)
    : QWidget(parent)
{
    m_edit = new QLineEdit(this);
    m_edit->installEventFilter(this);

    m_crumbFrame = new QFrame(this);
    m_crumbFrame->setObjectName(QStringLiteral("crumbs"));
    m_crumbFrame->setFrameShape(QFrame::StyledPanel);
    m_crumbFrame->setCursor(Qt::IBeamCursor);
    m_crumbFrame->setMinimumHeight(m_edit->sizeHint().height());
    // Long paths elide rather than widen the panel.
    m_crumbFrame->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_crumbFrame->installEventFilter(this);
    m_crumbLayout = new QHBoxLayout(m_crumbFrame);
    m_crumbLayout->setContentsMargins(2, 0, 2, 0);
    m_crumbLayout->setSpacing(0);

    m_stack = new QStackedLayout(this);
    m_stack->setContentsMargins(0, 0, 0, 0);
    m_stack->addWidget(m_crumbFrame);
    m_stack->addWidget(m_edit);
    m_stack->setCurrentWidget(m_crumbFrame);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

    connect(m_edit, &QLineEdit::returnPressed, this, [this] {
        const QString text = m_edit->text().trimmed();
        stopEditing();
        if (!text.isEmpty()) {
            emit pathRequested(text);
        }
    });

    applyStyle();
}

QLineEdit *PathBar::lineEdit() const
{
    return m_edit;
}

void PathBar::setLocation(const QString &editText, const QList<Crumb> &crumbs)
{
    m_editText = editText;
    if (m_stack->currentWidget() != m_edit) {
        m_edit->setText(editText);
    }
    if (crumbs.size() == m_crumbs.size()) {
        bool same = true;
        for (qsizetype i = 0; i < crumbs.size(); ++i) {
            if (crumbs[i].label != m_crumbs[i].label || crumbs[i].path != m_crumbs[i].path) {
                same = false;
                break;
            }
        }
        if (same) {
            return;
        }
    }
    m_crumbs = crumbs;
    rebuild();
}

void PathBar::setActive(bool active)
{
    m_active = active;
    applyStyle();
}

void PathBar::startEditing()
{
    m_edit->setText(m_editText);
    m_stack->setCurrentWidget(m_edit);
    m_edit->selectAll();
    m_edit->setFocus();
}

void PathBar::stopEditing()
{
    if (m_stack->currentWidget() == m_crumbFrame) {
        return;
    }
    m_edit->setText(m_editText);
    m_stack->setCurrentWidget(m_crumbFrame);
}

void PathBar::applyStyle()
{
    // The active panel keeps the highlighted bar it had before breadcrumbs.
    const QString fill = m_active ? QStringLiteral("palette(highlight)") : QStringLiteral("palette(base)");
    const QString text = m_active ? QStringLiteral("palette(highlighted-text)") : QStringLiteral("palette(text)");
    // Hover inverts on the blue active bar and turns blue on the inactive one.
    const QString hoverFill = m_active ? text : QStringLiteral("palette(highlight)");
    const QString hoverText = m_active ? fill : QStringLiteral("palette(highlighted-text)");
    m_crumbFrame->setStyleSheet(QStringLiteral(
        "QFrame#crumbs { background: %1; }"
        "QFrame#crumbs QLabel { color: %2; background: transparent; }"
        "QFrame#crumbs QToolButton { color: %2; background: transparent; border: none;"
        " border-radius: 2px; padding: 1px 0px; margin: 0; }"
        "QFrame#crumbs QToolButton:hover { background: %3; color: %4; }"
        "QFrame#crumbs QToolButton:pressed { background: %3; color: %4; }"
        "QFrame#crumbs QToolButton::menu-indicator { image: none; width: 0; }")
        .arg(fill, text, hoverFill, hoverText));
}

QLabel *PathBar::makeSeparator()
{
    auto *label = new QLabel(QString(QDir::separator()), m_crumbFrame);
    label->setContentsMargins(0, 0, 0, 0);
    return label;
}

void PathBar::rebuild()
{
    for (const Segment &segment : std::as_const(m_segments)) {
        delete segment.button;
        delete segment.separator;
    }
    m_segments.clear();
    delete m_overflow;
    m_overflow = nullptr;
    delete m_overflowSeparator;
    m_overflowSeparator = nullptr;
    while (QLayoutItem *item = m_crumbLayout->takeAt(0)) {
        delete item;
    }

    for (qsizetype i = 0; i < m_crumbs.size(); ++i) {
        const Crumb &crumb = m_crumbs[i];
        Segment segment;
        segment.path = crumb.path;
        if (i > 0) {
            segment.separator = makeSeparator();
            m_crumbLayout->addWidget(segment.separator);
        }
        segment.button = new QToolButton(m_crumbFrame);
        segment.button->setText(crumb.label);
        segment.button->setToolTip(crumb.path);
        segment.button->setFocusPolicy(Qt::NoFocus);
        segment.button->setCursor(Qt::ArrowCursor);
        segment.button->setEnabled(!crumb.path.isEmpty());
        fitToText(segment.button);
        const QString path = crumb.path;
        connect(segment.button, &QToolButton::clicked, this, [this, path] { emit pathRequested(path); });
        m_crumbLayout->addWidget(segment.button);
        m_segments.append(segment);

        if (i == 0) {
            // Stands in for the segments elided after the root.
            m_overflowSeparator = makeSeparator();
            m_crumbLayout->addWidget(m_overflowSeparator);
            m_overflow = new QToolButton(m_crumbFrame);
            m_overflow->setText(QStringLiteral("…"));
            m_overflow->setFocusPolicy(Qt::NoFocus);
            m_overflow->setCursor(Qt::ArrowCursor);
            m_overflow->setPopupMode(QToolButton::InstantPopup);
            m_overflow->setMenu(new QMenu(m_overflow));
            fitToText(m_overflow);
            m_crumbLayout->addWidget(m_overflow);
        }
    }
    m_crumbLayout->addStretch(1);
    updateElision();
}

void PathBar::updateElision()
{
    if (m_segments.isEmpty()) {
        return;
    }
    const QMargins margins = m_crumbLayout->contentsMargins();
    const int available = m_crumbFrame->contentsRect().width() - margins.left() - margins.right()
        - EditClickReservePx;

    int total = 0;
    for (const Segment &segment : std::as_const(m_segments)) {
        total += segment.button->sizeHint().width();
        if (segment.separator != nullptr) {
            total += segment.separator->sizeHint().width();
        }
    }

    // Hide segments from just after the root until the rest fit. Root and last always stay.
    const qsizetype last = m_segments.size() - 1;
    qsizetype hiddenEnd = 1;
    if (total > available && last > 1) {
        total += m_overflow->sizeHint().width() + m_overflowSeparator->sizeHint().width();
        while (hiddenEnd < last && total > available) {
            total -= m_segments[hiddenEnd].button->sizeHint().width();
            total -= m_segments[hiddenEnd].separator->sizeHint().width();
            ++hiddenEnd;
        }
    }

    QMenu *menu = m_overflow->menu();
    menu->clear();
    for (qsizetype i = 1; i < m_segments.size(); ++i) {
        const bool hidden = i < hiddenEnd;
        m_segments[i].button->setVisible(!hidden);
        m_segments[i].separator->setVisible(!hidden);
        if (hidden) {
            const QString path = m_segments[i].path;
            QAction *action = menu->addAction(m_segments[i].button->text());
            connect(action, &QAction::triggered, this, [this, path] { emit pathRequested(path); });
        }
    }
    const bool elided = hiddenEnd > 1;
    m_overflow->setVisible(elided);
    m_overflowSeparator->setVisible(elided);
}

void PathBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateElision();
}

bool PathBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_crumbFrame && event->type() == QEvent::MouseButtonPress) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            startEditing();
            return true;
        }
    }
    if (watched == m_edit) {
        if (event->type() == QEvent::KeyPress
            && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            stopEditing();
            emit editingCancelled();
            return true;
        }
        if (event->type() == QEvent::FocusOut) {
            stopEditing();
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace bifrons
