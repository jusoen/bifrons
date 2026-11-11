#pragma once

#include <QMainWindow>
#include <QPointer>

#include <memory>

#include "core/fileoperation.h"
#include "core/localfileoperation.h"

class QSplitter;

namespace dcpp {

class CommandRegistry;
class FilePanel;
class FindDialog;
class LocalFileSource;
class OperationRunner;
class PanelTabs;

// Top-level window with two side-by-side panels.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createCommands();
    void setActivePanel(FilePanel *panel);
    PanelTabs *activeSide() const;
    FilePanel *otherPanel() const;

    void cmCopy();
    void cmRename();
    void cmRenameOnly();
    void cmMakeDir();
    void cmDelete();
    void cmRefresh();

    void cmSearch();
    void transfer(LocalFileOperation::Kind kind, const QStringList &sources, const QString &baseDir,
                  const QString &title, const QString &label, const QString &defaultDestination);
    void runOperation(FileOperation *op, const QString &title);
    void onOperationFinished(const FileOperation::Result &result);

    void loadSettings();
    void saveSettings() const;

    std::shared_ptr<LocalFileSource> m_source;
    CommandRegistry *m_commands = nullptr;
    QSplitter *m_splitter = nullptr;
    PanelTabs *m_left = nullptr;
    PanelTabs *m_right = nullptr;
    PanelTabs *m_activeSide = nullptr;
    QPointer<FilePanel> m_active;
    OperationRunner *m_runner = nullptr;
    FindDialog *m_find = nullptr;
    bool m_closePending = false;
};

} // namespace dcpp
