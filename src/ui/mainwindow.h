#pragma once

#include <QMainWindow>

#include <memory>

#include "core/fileoperation.h"
#include "core/localfileoperation.h"

class QSplitter;

namespace dcpp {

class CommandRegistry;
class FilePanel;
class LocalFileSource;
class OperationRunner;

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
    FilePanel *otherPanel() const;

    void cmCopy();
    void cmRename();
    void cmRenameOnly();
    void cmMakeDir();
    void cmDelete();
    void cmRefresh();

    void transfer(LocalFileOperation::Kind kind, const QStringList &sources, const QString &title,
                  const QString &label, const QString &defaultDestination);
    void runOperation(FileOperation *op, const QString &title);
    void onOperationFinished(const FileOperation::Result &result);

    void loadSettings();
    void saveSettings() const;

    std::shared_ptr<LocalFileSource> m_source;
    CommandRegistry *m_commands = nullptr;
    QSplitter *m_splitter = nullptr;
    FilePanel *m_left = nullptr;
    FilePanel *m_right = nullptr;
    FilePanel *m_active = nullptr;
    OperationRunner *m_runner = nullptr;
    bool m_closePending = false;
};

} // namespace dcpp
