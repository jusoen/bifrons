#pragma once

#include <QDialog>

#include "core/usertools.h"

class QLineEdit;
class QPushButton;

namespace bifrons {

// Adds or edits one user toolbar button.
class UserToolDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UserToolDialog(const UserTool &tool, QWidget *parent = nullptr);

    UserTool tool() const;

private:
    void browseProgram();
    void browseIcon();
    void updateOk();

    QLineEdit *m_name = nullptr;
    QLineEdit *m_program = nullptr;
    QLineEdit *m_arguments = nullptr;
    QLineEdit *m_icon = nullptr;
    QPushButton *m_ok = nullptr;
};

} // namespace bifrons
