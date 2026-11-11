#include <QApplication>

#include "core/appinfo.h"
#include "ui/mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(dcpp::appOrganisation());
    QApplication::setApplicationDisplayName(dcpp::appDisplayName());
    QApplication::setApplicationVersion(dcpp::appVersion());
    QApplication::setOrganizationName(dcpp::appOrganisation());

    dcpp::MainWindow window;
    window.show();
    return QApplication::exec();
}
