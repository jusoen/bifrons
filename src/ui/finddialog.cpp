#include "ui/finddialog.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateEdit>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "core/localfilesource.h"

namespace dcpp {

namespace {

constexpr unsigned long ThreadJoinMs = 5000;
constexpr int MaxSizeKb = 2'000'000'000;

} // namespace

FindDialog::FindDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Find Files"));
    setModal(false);
    resize(700, 600);

    m_startDir = new QLineEdit(this);
    auto *browseButton = new QPushButton(tr("…"), this);
    browseButton->setAutoDefault(false);
    auto *dirRow = new QHBoxLayout;
    dirRow->addWidget(m_startDir, 1);
    dirRow->addWidget(browseButton);

    m_mask = new QLineEdit(QStringLiteral("*.*"), this);
    m_mask->setToolTip(tr("Separate several masks with ;"));

    m_text = new QLineEdit(this);
    m_caseSensitive = new QCheckBox(tr("Case sensitive"), this);
    m_encoding = new QComboBox(this);
    m_encoding->addItem(QStringLiteral("UTF-8"), QStringConverter::Utf8);
    m_encoding->addItem(QStringLiteral("UTF-16 LE"), QStringConverter::Utf16LE);
    m_encoding->addItem(QStringLiteral("UTF-16 BE"), QStringConverter::Utf16BE);
    m_encoding->addItem(QStringLiteral("Latin-1"), QStringConverter::Latin1);
    m_encoding->addItem(tr("System"), QStringConverter::System);
    auto *textOptions = new QHBoxLayout;
    textOptions->addWidget(m_caseSensitive);
    textOptions->addWidget(new QLabel(tr("Encoding:"), this));
    textOptions->addWidget(m_encoding);
    textOptions->addStretch();

    auto makeSize = [this](QCheckBox *&check, QSpinBox *&spin, const QString &label) {
        check = new QCheckBox(label, this);
        spin = new QSpinBox(this);
        spin->setRange(0, MaxSizeKb);
        spin->setSuffix(tr(" KB"));
        spin->setEnabled(false);
        connect(check, &QCheckBox::toggled, spin, &QWidget::setEnabled);
    };
    makeSize(m_useMinSize, m_minSize, tr("At least"));
    makeSize(m_useMaxSize, m_maxSize, tr("At most"));
    auto *sizeRow = new QHBoxLayout;
    sizeRow->addWidget(m_useMinSize);
    sizeRow->addWidget(m_minSize);
    sizeRow->addSpacing(16);
    sizeRow->addWidget(m_useMaxSize);
    sizeRow->addWidget(m_maxSize);
    sizeRow->addStretch();

    auto makeDate = [this](QCheckBox *&check, QDateEdit *&edit, const QString &label, const QDate &initial) {
        check = new QCheckBox(label, this);
        edit = new QDateEdit(initial, this);
        edit->setCalendarPopup(true);
        edit->setEnabled(false);
        connect(check, &QCheckBox::toggled, edit, &QWidget::setEnabled);
    };
    const QDate today = QDate::currentDate();
    makeDate(m_useAfter, m_after, tr("Modified on or after"), today.addMonths(-1));
    makeDate(m_useBefore, m_before, tr("on or before"), today);
    auto *dateRow = new QHBoxLayout;
    dateRow->addWidget(m_useAfter);
    dateRow->addWidget(m_after);
    dateRow->addSpacing(16);
    dateRow->addWidget(m_useBefore);
    dateRow->addWidget(m_before);
    dateRow->addStretch();

    auto *form = new QFormLayout;
    form->addRow(tr("Search in:"), dirRow);
    form->addRow(tr("File name:"), m_mask);
    form->addRow(tr("Containing text:"), m_text);
    form->addRow(QString(), textOptions);
    form->addRow(tr("Size:"), sizeRow);
    form->addRow(tr("Date:"), dateRow);

    m_results = new QListWidget(this);
    m_results->setUniformItemSizes(true);
    m_status = new QLabel(this);

    m_start = new QPushButton(tr("Start"), this);
    m_start->setDefault(true);
    m_stop = new QPushButton(tr("Stop"), this);
    m_stop->setAutoDefault(false);
    m_goTo = new QPushButton(tr("Go to file"), this);
    m_goTo->setAutoDefault(false);
    m_feed = new QPushButton(tr("Feed to panel"), this);
    m_feed->setAutoDefault(false);
    auto *closeButton = new QPushButton(tr("Close"), this);
    closeButton->setAutoDefault(false);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_start);
    buttons->addWidget(m_stop);
    buttons->addStretch();
    buttons->addWidget(m_goTo);
    buttons->addWidget(m_feed);
    buttons->addWidget(closeButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_results, 1);
    layout->addWidget(m_status);
    layout->addLayout(buttons);

    connect(browseButton, &QPushButton::clicked, this, &FindDialog::browse);
    connect(m_start, &QPushButton::clicked, this, &FindDialog::startSearch);
    connect(m_stop, &QPushButton::clicked, this, &FindDialog::stopSearch);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
    connect(m_goTo, &QPushButton::clicked, this, [this] {
        const QString path = selectedPath();
        if (!path.isEmpty()) {
            emit goToFile(path);
            close();
        }
    });
    connect(m_results, &QListWidget::itemActivated, m_goTo, &QPushButton::click);
    connect(m_results, &QListWidget::currentRowChanged, this, &FindDialog::updateButtons);
    connect(m_feed, &QPushButton::clicked, this, [this] {
        if (!m_found.isEmpty()) {
            emit feedToPanel(m_searchedDir, m_found);
            close();
        }
    });

    updateButtons();
}

FindDialog::~FindDialog()
{
    // The owner waits for searchFinished() before destroying a searching dialog.
    if (m_thread.isRunning()) {
        stopSearch();
        m_thread.quit();
        if (!m_thread.wait(ThreadJoinMs)) {
            qWarning("Search thread did not stop within %lu ms", ThreadJoinMs);
        }
    }
    delete m_engine;
}

void FindDialog::setStartDir(const QString &dir)
{
    if (!isSearching()) {
        m_startDir->setText(QDir::toNativeSeparators(dir));
    }
}

bool FindDialog::isSearching() const
{
    return m_engine != nullptr;
}

void FindDialog::stopSearch()
{
    if (m_engine != nullptr) {
        m_engine->cancel();
        m_status->setText(tr("Stopping…"));
    }
}

void FindDialog::closeEvent(QCloseEvent *event)
{
    stopSearch();
    QDialog::closeEvent(event);
}

SearchCriteria FindDialog::criteria() const
{
    SearchCriteria c;
    c.startDir = LocalFileSource::normalise(m_startDir->text().trimmed());
    c.nameMask = m_mask->text();
    c.text = m_text->text();
    c.textCase = m_caseSensitive->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    c.encoding = static_cast<QStringConverter::Encoding>(m_encoding->currentData().toInt());
    if (m_useMinSize->isChecked()) {
        c.minSize = qint64(m_minSize->value()) * 1024;
    }
    if (m_useMaxSize->isChecked()) {
        c.maxSize = qint64(m_maxSize->value()) * 1024;
    }
    if (m_useAfter->isChecked()) {
        c.modifiedAfter = m_after->date().startOfDay();
    }
    if (m_useBefore->isChecked()) {
        c.modifiedBefore = m_before->date().endOfDay();
    }
    return c;
}

void FindDialog::startSearch()
{
    if (isSearching()) {
        return;
    }
    const SearchCriteria c = criteria();
    m_results->clear();
    m_found.clear();
    m_searchedDir = c.startDir;

    m_engine = new SearchEngine(c);
    m_engine->moveToThread(&m_thread);
    connect(&m_thread, &QThread::started, m_engine, &SearchEngine::run);
    connect(m_engine, &SearchEngine::found, this, &FindDialog::onFound);
    connect(m_engine, &SearchEngine::progress, this, &FindDialog::onProgress);
    connect(m_engine, &SearchEngine::finished, this, &FindDialog::onEngineFinished);
    m_status->setText(tr("Searching…"));
    updateButtons();
    m_thread.start();
}

void FindDialog::onFound(const QStringList &paths)
{
    m_found.append(paths);
    for (const QString &path : paths) {
        m_results->addItem(QDir::toNativeSeparators(path));
    }
    updateButtons();
}

void FindDialog::onProgress(const QString &dir, int scanned)
{
    if (m_engine != nullptr && !m_engine->isCancelled()) {
        m_status->setText(tr("%1 scanned, %2 found. %3")
                              .arg(scanned)
                              .arg(m_found.size())
                              .arg(QDir::toNativeSeparators(dir)));
    }
}

void FindDialog::onEngineFinished()
{
    m_thread.quit();
    // run() has returned, so the thread only has to leave its event loop.
    if (!m_thread.wait(ThreadJoinMs)) {
        qWarning("Search thread did not stop within %lu ms", ThreadJoinMs);
    }
    disconnect(&m_thread, &QThread::started, m_engine, nullptr);
    const SearchSummary s = m_engine->summary();
    delete m_engine;
    m_engine = nullptr;

    QString text = s.cancelled ? tr("Stopped. ") : QString();
    text += tr("%1 found, %2 scanned.").arg(s.found).arg(s.scanned);
    if (s.truncated) {
        text += tr(" Stopped at %1 results.").arg(SearchEngine::MaxResults);
    }
    if (!s.errors.isEmpty()) {
        text += tr(" %1 could not be searched.").arg(s.errors.size());
        m_status->setToolTip(s.errors.mid(0, 50).join(QLatin1Char('\n')));
    } else {
        m_status->setToolTip(QString());
    }
    m_status->setText(text);
    updateButtons();
    emit searchFinished();
}

void FindDialog::browse()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Search in"), m_startDir->text());
    if (!dir.isEmpty()) {
        m_startDir->setText(QDir::toNativeSeparators(dir));
    }
}

void FindDialog::updateButtons()
{
    const bool searching = isSearching();
    m_start->setEnabled(!searching);
    m_stop->setEnabled(searching);
    m_goTo->setEnabled(m_results->currentRow() >= 0);
    m_feed->setEnabled(!searching && !m_found.isEmpty());
}

QString FindDialog::selectedPath() const
{
    const int row = m_results->currentRow();
    return (row >= 0 && row < m_found.size()) ? m_found.at(row) : QString();
}

} // namespace dcpp
