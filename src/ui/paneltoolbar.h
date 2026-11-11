#pragma once

#include <QMetaObject>
#include <QPointer>
#include <QList>
#include <QWidget>

#include "core/usertools.h"

class QComboBox;
class QHBoxLayout;
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
    // Rebuilds the user-defined buttons after the built-in ones.
    void setUserTools(const UserToolList &tools);

signals:
    void userToolClicked(int index);
    // index is -1 to add a new button.
    void userToolEditRequested(int index);
    void userToolRemoveRequested(int index);
    void userToolMoveRequested(int from, int to);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void refresh();
    // File the button's icon comes from: the chosen icon, else the program found on PATH.
    static QString iconSource(const UserTool &tool);
    void applyUserToolIcon(int index);

    QPointer<FilePanel> m_panel;
    QMetaObject::Connection m_panelConnection;
    QComboBox *m_drives = nullptr;
    QToolButton *m_back = nullptr;
    QToolButton *m_forward = nullptr;
    QToolButton *m_up = nullptr;
    QToolButton *m_home = nullptr;
    QToolButton *m_terminal = nullptr;
    QHBoxLayout *m_userRow = nullptr;
    UserToolList m_userTools;
    QList<QToolButton *> m_userButtons;
};

} // namespace bifrons
