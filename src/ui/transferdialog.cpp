#include "ui/transferdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QScreen>
#include <QVBoxLayout>

#include <algorithm>

namespace bifrons {

namespace {

constexpr int MinEditWidth = 360;
// Share of the screen width the dialog may grow to before the path scrolls instead.
constexpr qreal MaxScreenShare = 0.7;
// Room after the text so the cursor and the last character are not tight against the frame.
constexpr int EditSlack = 32;

} // namespace

TransferDialog::TransferDialog(const QString &title, const QString &label, const QString &destination,
                               bool offerDoNotAsk, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(label, this));
    m_edit = new QLineEdit(destination, this);
    m_edit->selectAll();
    layout->addWidget(m_edit);
    if (offerDoNotAsk) {
        m_doNotAsk = new QCheckBox(tr("Do not ask again"), this);
        layout->addWidget(m_doNotAsk);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    int maxWidth = MinEditWidth;
    if (const QScreen *s = parent != nullptr ? parent->screen() : screen(); s != nullptr) {
        maxWidth = std::max(MinEditWidth, static_cast<int>(s->availableGeometry().width() * MaxScreenShare));
    }
    const int textWidth = m_edit->fontMetrics().horizontalAdvance(destination) + EditSlack;
    m_edit->setMinimumWidth(std::clamp(textWidth, MinEditWidth, maxWidth));
}

QString TransferDialog::destination() const
{
    return m_edit->text();
}

bool TransferDialog::doNotAskAgain() const
{
    return m_doNotAsk != nullptr && m_doNotAsk->isChecked();
}

} // namespace bifrons
