#include <QApplication>
#include <QIcon>

#include "core/appinfo.h"
#include "ui/mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(dcpp::appOrganisation());
    QApplication::setApplicationDisplayName(dcpp::appDisplayName());
    QApplication::setApplicationVersion(dcpp::appVersion());
    QApplication::setOrganizationName(dcpp::appOrganisation());
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/dcpp.png")));

    dcpp::MainWindow window;
    window.show();
    return QApplication::exec();
}
