#pragma once

#include <QMetaObject>
#include <QPointer>
#include <QWidget>

class QComboBox;
class QToolButton;

namespace bifrons {

class FilePanel;

// Drive selector and navigation buttons above one side's tabs. Acts on that side's current tab.
class PanelToolbar : public QWidget
{
    Q_OBJECT

public:
    explicit PanelToolbar(QWidget *parent = nullptr);

    // Follows panel until the next call. nullptr disables the buttons.
    void setPanel(FilePanel *panel);

private:
    void refresh();

    QPointer<FilePanel> m_panel;
    QMetaObject::Connection m_panelConnection;
    QComboBox *m_drives = nullptr;
    QToolButton *m_back = nullptr;
    QToolButton *m_forward = nullptr;
    QToolButton *m_up = nullptr;
    QToolButton *m_home = nullptr;
    QToolButton *m_terminal = nullptr;
};

} // namespace bifrons
