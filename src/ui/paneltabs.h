#pragma once

#include <QTabWidget>

#include <memory>

#include "core/filesort.h"
#include "core/filesource.h"

namespace dcpp {

class FilePanel;

// Saved state of one tab.
struct TabState
{
    QString path;
    SortColumn sortColumn = SortColumn::Name;
    Qt::SortOrder sortOrder = Qt::AscendingOrder;
};

// One side of the main window: a FilePanel per tab. Always holds at least one tab.
class PanelTabs : public QTabWidget
{
    Q_OBJECT

public:
    explicit PanelTabs(std::shared_ptr<const FileSource> source, QWidget *parent = nullptr);

    FilePanel *currentPanel() const;
    QList<FilePanel *> panels() const;

    // Opens a tab after the current one showing the same folder, and switches to it.
    void newTab();
    // Closes the current tab unless it is the last one.
    void closeCurrentTab();
    void nextTab();
    void previousTab();

    QList<TabState> state() const;
    // Replaces all tabs. An empty list leaves a single tab at the home folder.
    void restore(const QList<TabState> &tabs, int currentIndex);

signals:
    void panelActivated(dcpp::FilePanel *panel);
    void switchPanelRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    FilePanel *addPanel(int index, const TabState &state);
    void closeTab(int index);
    void updateTitle(FilePanel *panel);

    std::shared_ptr<const FileSource> m_source;
};

} // namespace dcpp
