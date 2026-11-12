#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;

namespace bifrons {

// Asks where to copy or move to. Sized to fit the destination, up to most of the screen width.
class TransferDialog : public QDialog
{
    Q_OBJECT

public:
    // offerDoNotAsk adds a "Do not ask again" check box, for drag and drop.
    TransferDialog(const QString &title, const QString &label, const QString &destination, bool offerDoNotAsk,
                   QWidget *parent = nullptr);

    QString destination() const;
    // False when the check box was not offered.
    bool doNotAskAgain() const;

private:
    QLineEdit *m_edit = nullptr;
    QCheckBox *m_doNotAsk = nullptr;
};

} // namespace bifrons
