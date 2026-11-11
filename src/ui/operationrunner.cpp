#include "ui/operationrunner.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>

#include <algorithm>

namespace bifrons {

namespace {

constexpr int AnswerPollMs = 100;
constexpr unsigned long ThreadJoinMs = 5000;
constexpr int ProgressScale = 1000;

QString describe(const QString &path)
{
    const QFileInfo info(path);
    return QStringLiteral("%1\n%2, %3")
        .arg(QDir::toNativeSeparators(path),
             QLocale().formattedDataSize(info.size()),
             QLocale().toString(info.lastModified(), QLocale::ShortFormat));
}

} // namespace

OperationRunner::OperationRunner(FileOperation *op, const QString &title, QWidget *window)
    : QObject(window)
    , m_op(op)
    , m_window(window)
    , m_title(title)
{
    m_op->setParent(nullptr);
    m_op->moveToThread(&m_thread);
    m_op->setQueryHandler([this](const FileOperation::Query &q) { return askFromWorker(q); });

    connect(&m_thread, &QThread::started, m_op, &FileOperation::run);
    connect(m_op, &FileOperation::progress, this, &OperationRunner::onProgress);
    connect(m_op, &FileOperation::finished, this, &OperationRunner::onOperationFinished);
}

OperationRunner::~OperationRunner()
{
    Q_ASSERT(!m_thread.isRunning());
    delete m_op;
}

void OperationRunner::start()
{
    m_running = true;
    emit progressChanged(0, QString());
    m_thread.start();
}

void OperationRunner::cancel()
{
    m_op->cancel();
    emit progressChanged(0, tr("Cancelling…"));
    // Wake a worker waiting on a query so it sees the cancel straight away.
    const QMutexLocker lock(&m_mutex);
    m_answered.wakeAll();
}

bool OperationRunner::isRunning() const
{
    return m_running;
}

QString OperationRunner::title() const
{
    return m_title;
}

FileOperation::Answer OperationRunner::askFromWorker(const FileOperation::Query &query)
{
    QMutexLocker lock(&m_mutex);
    const quint64 id = ++m_queryId;
    m_answer.reset();
    QMetaObject::invokeMethod(this, [this, id, query] { showQuery(id, query); }, Qt::QueuedConnection);
    // Waits for the user, in short slices so a cancel is noticed within AnswerPollMs.
    while (!m_answer.has_value()) {
        if (m_op->isCancelled()) {
            return FileOperation::Answer::Abort;
        }
        (void)m_answered.wait(&m_mutex, QDeadlineTimer(AnswerPollMs));
    }
    return *m_answer;
}

void OperationRunner::showQuery(quint64 id, const FileOperation::Query &query)
{
    using Answer = FileOperation::Answer;
    QMessageBox box(m_window);
    QHash<QAbstractButton *, Answer> answers;
    auto addButton = [&](const QString &text, QMessageBox::ButtonRole role, Answer answer) {
        answers.insert(box.addButton(text, role), answer);
    };

    if (query.kind == FileOperation::Query::Kind::Collision) {
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(tr("File exists"));
        box.setText(tr("The target already exists. Overwrite it?"));
        box.setInformativeText(tr("New:\n%1\n\nExisting:\n%2").arg(describe(query.source), describe(query.target)));
        addButton(tr("Overwrite"), QMessageBox::AcceptRole, Answer::Overwrite);
        addButton(tr("Overwrite all"), QMessageBox::AcceptRole, Answer::OverwriteAll);
        addButton(tr("Skip"), QMessageBox::RejectRole, Answer::Skip);
        addButton(tr("Skip all"), QMessageBox::RejectRole, Answer::SkipAll);
        addButton(tr("Rename"), QMessageBox::ActionRole, Answer::Rename);
    } else {
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Error"));
        box.setText(QDir::toNativeSeparators(query.source));
        box.setInformativeText(query.message);
        addButton(tr("Retry"), QMessageBox::AcceptRole, Answer::Retry);
        addButton(tr("Skip"), QMessageBox::RejectRole, Answer::Skip);
        addButton(tr("Skip all"), QMessageBox::RejectRole, Answer::SkipAll);
    }
    QPushButton *abort = box.addButton(tr("Cancel"), QMessageBox::DestructiveRole);
    answers.insert(abort, Answer::Abort);
    box.setEscapeButton(abort);
    box.exec();

    const Answer answer = answers.value(box.clickedButton(), Answer::Abort);
    const QMutexLocker lock(&m_mutex);
    // Ignore an answer to a query the worker has already given up on.
    if (id == m_queryId) {
        m_answer = answer;
    }
    m_answered.wakeAll();
}

void OperationRunner::onProgress(qint64 done, qint64 total, const QString &current)
{
    if (m_op->isCancelled()) {
        return;
    }
    const int value = total > 0 ? static_cast<int>((done * ProgressScale) / total) : 0;
    emit progressChanged(std::clamp(value, 0, ProgressScale), QDir::toNativeSeparators(current));
}

void OperationRunner::onOperationFinished()
{
    m_thread.quit();
    // run() has returned, so the thread only has to leave its event loop.
    if (!m_thread.wait(ThreadJoinMs)) {
        qWarning("Operation thread did not stop within %lu ms", ThreadJoinMs);
    }
    m_running = false;
    emit finished(m_op->result());
}

} // namespace bifrons
