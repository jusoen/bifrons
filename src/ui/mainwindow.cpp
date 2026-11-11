#include "ui/mainwindow.h"

#include <QLabel>
#include <QSplitter>
#include <QStatusBar>

#include "core/appinfo.h"

namespace dcpp {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    auto *left = new QLabel(tr("Left panel"), splitter);
    auto *right = new QLabel(tr("Right panel"), splitter);
    left->setAlignment(Qt::AlignCenter);
    right->setAlignment(Qt::AlignCenter);
    splitter->addWidget(left);
    splitter->addWidget(right);
    setCentralWidget(splitter);

    statusBar()->showMessage(appDisplayName() + QLatin1Char(' ') + appVersion());
    resize(1100, 700);
}

} // namespace dcpp
