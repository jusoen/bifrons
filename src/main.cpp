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
    QApplication::setApplicationDisplayName(bifrons::appDisplayName());
    QApplication::setApplicationVersion(bifrons::appVersion());
    QApplication::setOrganizationName(bifrons::appOrganisation());
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/bifrons.png")));
    migrateLegacySettings();

    bifrons::MainWindow window;
    window.show();
    return QApplication::exec();
}
