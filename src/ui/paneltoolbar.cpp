#include "ui/paneltoolbar.h"

#include <QComboBox>
#include <QContextMenuEvent>
#include <QMenu>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QToolButton>

#include "core/localfilesource.h"
#include "ui/fileicons.h"
#include "ui/filepanel.h"

namespace bifrons {

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
    if (!icon.isNull()) {
        button->setIcon(withFadedDisabled(icon, button->iconSize(), parent->devicePixelRatioF()));
    }
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
    // User buttons sit on the left, so adding one never shifts the built-in buttons on the right.
    m_userRow = new QHBoxLayout;
    m_userRow->setContentsMargins(0, 0, 0, 0);
    m_userRow->setSpacing(row->spacing());
    row->addLayout(m_userRow);
    row->addStretch(1);
    m_back = makeButton(this, QIcon(), tr("Back (Alt+Left)"));
    m_forward = makeButton(this, QIcon(), tr("Forward (Alt+Right)"));
    m_up = makeButton(this, QIcon(), tr("Parent folder"));
    m_home = makeButton(this, QIcon(), tr("Home folder"));
    m_terminal = makeButton(this, QIcon(), tr("Open terminal here"));
    refreshIcons();
    row->addWidget(m_back);
    row->addWidget(m_forward);
    row->addWidget(m_up);
    row->addSpacing(6);
    row->addWidget(m_home);
    row->addWidget(m_terminal);
    connect(&FileIcons::instance(), &FileIcons::iconReady, this, [this](const QString &path) {
        for (int i = 0; i < m_userTools.size(); ++i) {
            if (iconSource(m_userTools.at(i)).compare(path, Qt::CaseInsensitive) == 0) {
                applyUserToolIcon(i);
            }
        }
    });

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

void PanelToolbar::refreshIcons()
{
    const qreal dpr = devicePixelRatioF();
    const auto apply = [dpr](QToolButton *button, const QIcon &icon) {
        button->setIcon(withFadedDisabled(icon, button->iconSize(), dpr));
    };
    apply(m_back, themeIcon(QIcon::ThemeIcon::GoPrevious, QStyle::SP_ArrowBack, this));
    apply(m_forward, themeIcon(QIcon::ThemeIcon::GoNext, QStyle::SP_ArrowForward, this));
    apply(m_up, themeIcon(QIcon::ThemeIcon::GoUp, QStyle::SP_FileDialogToParent, this));
    apply(m_home, themeIcon(QIcon::ThemeIcon::GoHome, QStyle::SP_DirHomeIcon, this));
    apply(m_terminal, terminalIcon(this));
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

QString PanelToolbar::iconSource(const UserTool &tool)
{
    if (!tool.icon.isEmpty()) {
        return tool.icon;
    }
    if (!tool.program.contains(QLatin1Char('/')) && !tool.program.contains(QLatin1Char('\\'))) {
        const QString onPath = QStandardPaths::findExecutable(tool.program);
        if (!onPath.isEmpty()) {
            return QDir::fromNativeSeparators(onPath);
        }
    }
    return tool.program;
}

void PanelToolbar::applyUserToolIcon(int index)
{
    if (index < 0 || index >= m_userButtons.size()) {
        return;
    }
    const QString source = iconSource(m_userTools.at(index));
    QIcon icon;
    const QString suffix = QFileInfo(source).suffix().toLower();
    if (!m_userTools.at(index).icon.isEmpty() && suffix != QLatin1String("exe")) {
        icon = QIcon(source);
    }
    if (icon.isNull()) {
        // The shell icon: a program's own icon arrives later through FileIcons::iconReady.
        FileItem item;
        item.name = QFileInfo(source).fileName();
        icon = FileIcons::instance().icon(item, source);
    }
    QToolButton *button = m_userButtons.at(index);
    button->setIcon(withFadedDisabled(icon, button->iconSize(), devicePixelRatioF()));
}

void PanelToolbar::setUserTools(const UserToolList &tools)
{
    qDeleteAll(m_userButtons);
    m_userButtons.clear();
    m_userTools = tools;
    for (int i = 0; i < tools.size(); ++i) {
        const UserTool &tool = tools.at(i);
        QToolButton *button = makeButton(this, QIcon(), tool.name.isEmpty() ? tool.program : tool.name);
        m_userRow->addWidget(button);
        m_userButtons.append(button);
        connect(button, &QToolButton::clicked, this, [this, i] { emit userToolClicked(i); });
        applyUserToolIcon(i);
    }
    // A small gap separates user buttons from the drive selector.
    m_userRow->setContentsMargins(tools.isEmpty() ? 0 : 6, 0, 0, 0);
    refresh();
}

void PanelToolbar::contextMenuEvent(QContextMenuEvent *event)
{
    int index = -1;
    for (int i = 0; i < m_userButtons.size(); ++i) {
        if (m_userButtons.at(i)->geometry().contains(event->pos())) {
            index = i;
            break;
        }
    }
    QMenu menu(this);
    if (index >= 0) {
        connect(menu.addAction(tr("&Edit Button")), &QAction::triggered, this,
                [this, index] { emit userToolEditRequested(index); });
        QAction *left = menu.addAction(tr("Move &Left"));
        left->setEnabled(index > 0);
        connect(left, &QAction::triggered, this, [this, index] { emit userToolMoveRequested(index, index - 1); });
        QAction *right = menu.addAction(tr("Move &Right"));
        right->setEnabled(index + 1 < m_userButtons.size());
        connect(right, &QAction::triggered, this, [this, index] { emit userToolMoveRequested(index, index + 1); });
        connect(menu.addAction(tr("Re&move Button")), &QAction::triggered, this,
                [this, index] { emit userToolRemoveRequested(index); });
        menu.addSeparator();
    }
    connect(menu.addAction(tr("&Add Button")), &QAction::triggered, this, [this] { emit userToolEditRequested(-1); });
    menu.exec(event->globalPos());
}

void PanelToolbar::refresh()
{
    const bool has = m_panel != nullptr;
    m_back->setEnabled(has && m_panel->canGoBack());
    m_forward->setEnabled(has && m_panel->canGoForward());
    m_up->setEnabled(has && m_panel->canGoUp());
    m_home->setEnabled(has);
    m_terminal->setEnabled(has && m_panel->canOpenTerminal());
    // A tool starts in the panel's folder, so it waits for the same reachable folder as the terminal.
    for (QToolButton *button : std::as_const(m_userButtons)) {
        button->setEnabled(has && m_panel->canOpenTerminal());
    }
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

} // namespace bifrons
