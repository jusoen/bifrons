#pragma once

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QTimer>

#include <memory>

#include "core/favourites.h"
#include "core/fileoperation.h"
#include "core/localfileoperation.h"
#include "core/renameengine.h"

class QMenu;
class QSplitter;
class QStackedWidget;

namespace bifrons {

class CommandRegistry;
class FilePanel;
class FindDialog;
class LocalFileSource;
class OperationQueue;
class PanelTabs;
class ViewerWidget;

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
    void createMenus();
    void fillFavouritesMenu(QMenu *menu);
    void saveFavouritesNow() const;
    void setActivePanel(FilePanel *panel);
    void setShowHidden(bool show);
    PanelTabs *activeSide() const;
    FilePanel *otherPanel() const;
    // Puts the drive and navigation row above a side's tabs.
    QWidget *wrapWithToolbar(PanelTabs *side);

    void cmCopy();
    void cmRename();
    void cmRenameOnly();
    void cmMakeDir();
    void cmDelete();
    void cmRefresh();

    void cmSearch();
    void cmView();
    void cmQuickView();
    void updateQuickView();
    void cmMultiRename();
    void transfer(LocalFileOperation::Kind kind, const QStringList &sources, const QString &baseDir,
                  const QString &title, const QString &label, const QString &defaultDestination);
    // Queues op on the active side. Each side runs its own operations one at a time in the background.
    void runOperation(FileOperation *op, const QString &title);
    void onOperationFinished(const FileOperation::Result &result);
    bool operationsBusy() const;

    void loadSettings();
    void saveSettings() const;

    std::shared_ptr<LocalFileSource> m_source;
    CommandRegistry *m_commands = nullptr;
    QSplitter *m_splitter = nullptr;
    PanelTabs *m_left = nullptr;
    PanelTabs *m_right = nullptr;
    PanelTabs *m_activeSide = nullptr;
    QPointer<FilePanel> m_active;
    FindDialog *m_find = nullptr;
    QStackedWidget *m_leftStack = nullptr;
    QStackedWidget *m_rightStack = nullptr;
    // Side currently replaced by the quick view, or nullptr when it is off.
    QStackedWidget *m_quickSide = nullptr;
    ViewerWidget *m_quickView = nullptr;
    QTimer m_quickTimer;
    QList<RenameStep> m_lastRename;
    FavouriteList m_favourites;
    bool m_closePending = false;
    // Each side's operation queue, shown as a progress strip under its tabs.
    QHash<PanelTabs *, OperationQueue *> m_queues;
    bool m_showHidden = false;
};

} // namespace bifrons
