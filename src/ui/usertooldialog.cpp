#include "ui/usertooldialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

namespace bifrons {

namespace {

// A line edit with a "…" button beside it.
QWidget *withBrowse(QLineEdit *edit, QWidget *parent, const std::function<void()> &browse)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(edit, 1);
    auto *button = new QToolButton(row);
    button->setText(QStringLiteral("…"));
    QObject::connect(button, &QToolButton::clicked, row, browse);
    layout->addWidget(button);
    return row;
}

} // namespace

UserToolDialog::UserToolDialog(const UserTool &tool, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tool.program.isEmpty() ? tr("Add Toolbar Button") : tr("Edit Toolbar Button"));
    setMinimumWidth(520);

    m_name = new QLineEdit(tool.name, this);
    m_name->setPlaceholderText(tr("Shown as the button's tooltip"));
    m_program = new QLineEdit(QDir::toNativeSeparators(tool.program), this);
    m_program->setPlaceholderText(tr("Program or script, e.g. notepad.exe or C:\\Tools\\backup.bat"));
    m_arguments = new QLineEdit(tool.arguments, this);
    m_arguments->setPlaceholderText(tr("e.g. %p, or --out %t %s"));
    m_icon = new QLineEdit(QDir::toNativeSeparators(tool.icon), this);
    m_icon->setPlaceholderText(tr("Optional. Leave empty to use the program's icon"));

    auto *help = new QLabel(tr("<b>%d</b> current folder &nbsp; <b>%p</b> file under the cursor &nbsp; "
                               "<b>%n</b> its name<br><b>%s</b> selected files &nbsp; "
                               "<b>%t</b> other pane's folder &nbsp; <b>%%</b> a literal %<br>"
                               "Paths with spaces are passed as one argument. The program starts in the "
                               "current folder."),
                            this);
    help->setTextFormat(Qt::RichText);
    help->setWordWrap(true);

    auto *form = new QFormLayout;
    form->addRow(tr("&Name:"), m_name);
    form->addRow(tr("&Program:"), withBrowse(m_program, this, [this] { browseProgram(); }));
    form->addRow(tr("&Arguments:"), m_arguments);
    form->addRow(QString(), help);
    form->addRow(tr("&Icon:"), withBrowse(m_icon, this, [this] { browseIcon(); }));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_ok = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_program, &QLineEdit::textChanged, this, &UserToolDialog::updateOk);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    updateOk();
}

UserTool UserToolDialog::tool() const
{
    UserTool tool;
    tool.program = QDir::fromNativeSeparators(m_program->text().trimmed());
    tool.name = m_name->text().trimmed();
    if (tool.name.isEmpty()) {
        tool.name = QFileInfo(tool.program).completeBaseName();
    }
    tool.arguments = m_arguments->text().trimmed();
    tool.icon = QDir::fromNativeSeparators(m_icon->text().trimmed());
    return tool;
}

void UserToolDialog::browseProgram()
{
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Choose Program"), QFileInfo(m_program->text()).absolutePath(),
        tr("Programs and scripts (*.exe *.com *.bat *.cmd *.ps1 *.py *.vbs);;All files (*)"));
    if (!file.isEmpty()) {
        m_program->setText(QDir::toNativeSeparators(file));
    }
}

void UserToolDialog::browseIcon()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Choose Icon"), QFileInfo(m_icon->text()).absolutePath(),
                                                      tr("Icons (*.ico *.png *.svg *.exe);;All files (*)"));
    if (!file.isEmpty()) {
        m_icon->setText(QDir::toNativeSeparators(file));
    }
}

void UserToolDialog::updateOk()
{
    m_ok->setEnabled(!m_program->text().trimmed().isEmpty());
}

} // namespace bifrons
