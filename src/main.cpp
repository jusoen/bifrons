#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QSettings>

#include "core/appinfo.h"
#include "ui/mainwindow.h"

namespace {

// The app was called DC++ and kept its settings under "dcpp". Copy them across once, so tabs and
// favourites survive the rename. The old file is left in place as a fallback.
void migrateLegacySettings()
{
    const QString current = QSettings(QSettings::IniFormat, QSettings::UserScope, bifrons::appOrganisation(),
                                      bifrons::appOrganisation()).fileName();
    const QString legacy = QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("dcpp"),
                                     QStringLiteral("dcpp")).fileName();
    if (QFileInfo::exists(current) || !QFileInfo::exists(legacy)) {
        return;
    }
    // On failure the app starts with default settings, which is the same as a first run.
    if (QDir().mkpath(QFileInfo(current).absolutePath())) {
        (void)QFile::copy(legacy, current);
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(bifrons::appOrganisation());
    // Empty, so Qt adds no " - Bifrons" to window titles. The main window sets its own.
    QApplication::setApplicationDisplayName(QString());
    QApplication::setApplicationVersion(bifrons::appVersion());
    QApplication::setOrganizationName(bifrons::appOrganisation());
    // ICO gives the pre-rendered small sizes. PNG adds 512 px and covers a missing ICO plugin.
    QIcon appIcon(QStringLiteral(":/icons/bifrons.ico"));
    appIcon.addFile(QStringLiteral(":/icons/bifrons.png"));
    QApplication::setWindowIcon(appIcon);
    migrateLegacySettings();

    bifrons::MainWindow window;
    window.show();
    return QApplication::exec();
}
