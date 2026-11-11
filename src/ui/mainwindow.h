#pragma once

#include <QMainWindow>

namespace dcpp {

// Top-level window with two side-by-side panels.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
};

} // namespace dcpp
