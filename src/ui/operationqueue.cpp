#include "ui/operationqueue.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QToolButton>

#include "ui/operationrunner.h"

namespace bifrons {

namespace {
constexpr int ProgressMax = 1000;
constexpr int ProgressWidthPx = 140;
} // namespace

OperationQueue::OperationQueue(QWidget *window, QWidget *parent)
    : QWidget(parent)
    , m_window(window)
{
    m_label = new QLabel(this);
    m_label->setTextFormat(Qt::PlainText);
    // Long file names elide rather than widen the side.
    m_label->setMinimumWidth(0);
    m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_bar = new QProgressBar(this);
    m_bar->setRange(0, ProgressMax);
    m_bar->setTextVisible(false);
    m_bar->setFixedWidth(ProgressWidthPx);
    m_cancel = new QToolButton(this);
    m_cancel->setText(tr("Cancel"));
    m_cancel->setAutoRaise(true);
    m_cancel->setFocusPolicy(Qt::NoFocus);
    connect(m_cancel, &QToolButton::clicked, this, [this] {
        if (m_runner != nullptr) {
            m_runner->cancel();
        }
    });

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(4, 0, 0, 0);
    row->addWidget(m_label, 1);
    row->addWidget(m_bar);
    row->addWidget(m_cancel);
    hide();
}

OperationQueue::~OperationQueue()
{
    for (const Pending &pending : std::as_const(m_pending)) {
        delete pending.op;
    }
}

void OperationQueue::enqueue(FileOperation *op, const QString &title)
{
    if (op == nullptr) {
        return;
    }
    m_pending.append({op, title});
    if (m_runner == nullptr) {
        startNext();
    } else {
        showProgress(m_bar->value(), QString());
    }
}

bool OperationQueue::isBusy() const
{
    return m_runner != nullptr;
}

void OperationQueue::cancelAll()
{
    for (const Pending &pending : std::as_const(m_pending)) {
        delete pending.op;
    }
    m_pending.clear();
    if (m_runner != nullptr) {
        m_runner->cancel();
    }
}

void OperationQueue::startNext()
{
    if (m_runner != nullptr) {
        return;
    }
    if (m_pending.isEmpty()) {
        hide();
        return;
    }
    const Pending next = m_pending.takeFirst();
    m_runner = new OperationRunner(next.op, next.title, m_window);
    connect(m_runner, &OperationRunner::progressChanged, this, &OperationQueue::showProgress);
    connect(m_runner, &OperationRunner::finished, this, &OperationQueue::onRunnerFinished);
    show();
    m_runner->start();
}

void OperationQueue::onRunnerFinished(const FileOperation::Result &result)
{
    m_runner->deleteLater();
    m_runner = nullptr;
    // Start the next one before anyone reports errors, so a message box does not hold up the queue.
    startNext();
    emit finished(result);
}

void OperationQueue::showProgress(int permille, const QString &current)
{
    if (m_runner == nullptr) {
        return;
    }
    QString text = QStringLiteral("%1: %2%").arg(m_runner->title()).arg(permille / 10);
    if (!current.isEmpty()) {
        text += QStringLiteral("  ") + current;
    }
    if (!m_pending.isEmpty()) {
        text += tr("  (%n more queued)", nullptr, static_cast<int>(m_pending.size()));
    }
    m_label->setText(text);
    m_label->setToolTip(text);
    m_bar->setValue(permille);
}

} // namespace bifrons
