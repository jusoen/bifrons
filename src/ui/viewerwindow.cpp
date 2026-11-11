#include "ui/viewerwindow.h"

#include <QCloseEvent>
#include <QFileInfo>
#include <QSettings>
#include <QShortcut>
#include <QVBoxLayout>

#include "core/appinfo.h"
#include "ui/viewerwidget.h"

namespace dcpp {

namespace {

constexpr qint64 ViewerMaxBytes = 32 * 1024 * 1024;

QSettings settingsStore()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, appOrganisation(), appOrganisation());
}

} // namespace

ViewerWindow::ViewerWindow(QStringList files, int index, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_files(std::move(files))
{
    setAttribute(Qt::WA_DeleteOnClose);
    m_viewer = new ViewerWidget(ViewerMaxBytes, this);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_viewer);

    const auto key = [this](const QKeySequence &seq, auto handler) {
        auto *shortcut = new QShortcut(seq, this);
        shortcut->setContext(Qt::WindowShortcut);
        connect(shortcut, &QShortcut::activated, this, handler);
    };
    key(Qt::Key_Escape, [this] { close(); });
    key(Qt::Key_1, [this] { m_viewer->setMode(ViewerWidget::Mode::Auto); });
    key(Qt::Key_2, [this] { m_viewer->setMode(ViewerWidget::Mode::Text); });
    key(Qt::Key_3, [this] { m_viewer->setMode(ViewerWidget::Mode::Hex); });
    key(Qt::Key_W, [this] { m_viewer->setWrap(!m_viewer->wrap()); });
    key(Qt::Key_N, [this] { showIndex(m_index + 1); });
    key(Qt::Key_P, [this] { showIndex(m_index - 1); });
    connect(m_viewer, &ViewerWidget::markdownLinkActivated, this, &ViewerWindow::showPath);

    resize(900, 700);
    const QByteArray geometry = settingsStore().value(QStringLiteral("viewer/geometry")).toByteArray();
    if (!geometry.isEmpty()) {
        // An invalid saved geometry just keeps the default size.
        (void)restoreGeometry(geometry);
    }
    showIndex(index);
}

void ViewerWindow::showIndex(int index)
{
    if (index < 0 || index >= m_files.size()) {
        return;
    }
    m_index = index;
    showPath(m_files.at(index));
}

void ViewerWindow::showPath(const QString &path)
{
    m_viewer->showFile(path);
    setWindowTitle(tr("%1 - %2 viewer").arg(QFileInfo(path).fileName(), appDisplayName()));
    m_viewer->setFocus();
}

void ViewerWindow::closeEvent(QCloseEvent *event)
{
    QSettings s = settingsStore();
    s.setValue(QStringLiteral("viewer/geometry"), saveGeometry());
    QWidget::closeEvent(event);
}

} // namespace dcpp
