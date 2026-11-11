#include "ui/paneltoolbar.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QToolButton>

#include "core/localfilesource.h"
#include "ui/filepanel.h"

namespace dcpp {

namespace {

QIcon themeIcon(QIcon::ThemeIcon theme, QStyle::StandardPixmap fallback, const QWidget *widget)
{
    const QIcon icon = QIcon::fromTheme(theme);
    return icon.isNull() ? widget->style()->standardIcon(fallback, nullptr, widget) : icon;
}

// No platform icon exists for a terminal, so draw a ">_" prompt in the text colour.
QIcon terminalIcon(const QWidget *widget)
{
    constexpr int Size = 32;
    QPixmap pixmap(Size, Size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(widget->palette().color(QPalette::WindowText), 2.5);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawRoundedRect(QRectF(2.5, 4.5, 27, 23), 3, 3);
    painter.drawPolyline(QPolygonF({QPointF(8, 11), QPointF(13, 16), QPointF(8, 21)}));
    painter.drawLine(QPointF(15, 21), QPointF(23, 21));
    painter.end();
    return QIcon(pixmap);
}

// Opacity of a disabled icon. The style's own greying is too close to the enabled colour.
constexpr qreal DisabledIconOpacity = 0.25;

// Normal pixmap as drawn, disabled pixmap faded well back so the two states are easy to tell apart.
QIcon withFadedDisabled(const QIcon &icon, const QSize &size, qreal dpr)
{
    const QPixmap normal = icon.pixmap(size, dpr, QIcon::Normal);
    QPixmap faded(normal.size());
    faded.setDevicePixelRatio(normal.devicePixelRatio());
    faded.fill(Qt::transparent);
    QPainter painter(&faded);
    painter.setOpacity(DisabledIconOpacity);
    painter.drawPixmap(0, 0, normal);
    painter.end();
    QIcon result;
    result.addPixmap(normal, QIcon::Normal);
    result.addPixmap(normal, QIcon::Active);
    result.addPixmap(faded, QIcon::Disabled);
    return result;
}

QToolButton *makeButton(QWidget *parent, const QIcon &icon, const QString &tip)
{
    auto *button = new QToolButton(parent);
    button->setToolTip(tip);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    // Rounded hover and press backgrounds, as on the menu bar.
    button->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; border-radius: 4px; padding: 3px; background: transparent; }"
        "QToolButton:hover { background: rgba(128, 128, 128, 55); }"
        "QToolButton:pressed { background: rgba(128, 128, 128, 100); }"
        "QToolButton:disabled { background: transparent; }"));
    button->setIcon(withFadedDisabled(icon, button->iconSize(), parent->devicePixelRatioF()));
    return button;
}

#if defined(Q_OS_WIN)
// Re-reads the drive list each time it opens, so new USB drives show up.
class DriveComboBox : public QComboBox
{
public:
    using QComboBox::QComboBox;

    void refresh()
    {
        const QString current = currentData().toString();
        clear();
        for (const QFileInfo &drive : QDir::drives()) {
            const QString root = drive.absoluteFilePath();
            addItem(QDir::toNativeSeparators(root).left(2), root);
        }
        const int i = findData(current);
        setCurrentIndex(i);
    }

    void showPopup() override
    {
        refresh();
        QComboBox::showPopup();
    }
};
#endif

} // namespace

PanelToolbar::PanelToolbar(QWidget *parent)
    : QWidget(parent)
{
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
#if defined(Q_OS_WIN)
    auto *drives = new DriveComboBox(this);
    drives->refresh();
    drives->setFocusPolicy(Qt::NoFocus);
    m_drives = drives;
    row->addWidget(m_drives);
    connect(m_drives, &QComboBox::activated, this, [this](int index) {
        const QString root = m_drives->itemData(index).toString();
        if (m_panel != nullptr && !root.isEmpty()) {
            m_panel->setPath(root);
            m_panel->focusView();
        }
    });
#endif
    row->addStretch(1);
    m_back = makeButton(this, themeIcon(QIcon::ThemeIcon::GoPrevious, QStyle::SP_ArrowBack, this),
                        tr("Back (Alt+Left)"));
    m_forward = makeButton(this, themeIcon(QIcon::ThemeIcon::GoNext, QStyle::SP_ArrowForward, this),
                           tr("Forward (Alt+Right)"));
    m_up = makeButton(this, themeIcon(QIcon::ThemeIcon::GoUp, QStyle::SP_FileDialogToParent, this),
                      tr("Parent folder"));
    m_home = makeButton(this, themeIcon(QIcon::ThemeIcon::GoHome, QStyle::SP_DirHomeIcon, this), tr("Home folder"));
    m_terminal = makeButton(this, terminalIcon(this), tr("Open terminal here"));
    row->addWidget(m_back);
    row->addWidget(m_forward);
    row->addWidget(m_up);
    row->addSpacing(6);
    row->addWidget(m_home);
    row->addWidget(m_terminal);

    connect(m_back, &QToolButton::clicked, this, [this] {
        if (m_panel != nullptr) {
            m_panel->goBack();
            m_panel->focusView();
        }
    });
    connect(m_forward, &QToolButton::clicked, this, [this] {
        if (m_panel != nullptr) {
            m_panel->goForward();
            m_panel->focusView();
        }
    });
    connect(m_up, &QToolButton::clicked, this, [this] {
        if (m_panel != nullptr) {
            m_panel->goParent();
            m_panel->focusView();
        }
    });
    connect(m_home, &QToolButton::clicked, this, [this] {
        if (m_panel != nullptr) {
            m_panel->setPath(LocalFileSource::normalise(QDir::homePath()));
            m_panel->focusView();
        }
    });
    connect(m_terminal, &QToolButton::clicked, this, [this] {
        if (m_panel != nullptr) {
            m_panel->openTerminalHere();
        }
    });

    refresh();
}

void PanelToolbar::setPanel(FilePanel *panel)
{
    disconnect(m_panelConnection);
    m_panel = panel;
    if (panel != nullptr) {
        m_panelConnection = connect(panel, &FilePanel::navigationChanged, this, &PanelToolbar::refresh);
    }
    refresh();
}

void PanelToolbar::refresh()
{
    const bool has = m_panel != nullptr;
    m_back->setEnabled(has && m_panel->canGoBack());
    m_forward->setEnabled(has && m_panel->canGoForward());
    m_up->setEnabled(has && m_panel->canGoUp());
    m_home->setEnabled(has);
    m_terminal->setEnabled(has && m_panel->canOpenTerminal());
    if (m_drives == nullptr) {
        return;
    }
    const QString path = has ? m_panel->persistentPath() : QString();
    for (int i = 0; i < m_drives->count(); ++i) {
        const QString root = m_drives->itemData(i).toString();
        if (!path.isEmpty() && (path.startsWith(root, Qt::CaseInsensitive) || path + QLatin1Char('/') == root)) {
            m_drives->setCurrentIndex(i);
            return;
        }
    }
    m_drives->setCurrentIndex(-1);
}

} // namespace dcpp
