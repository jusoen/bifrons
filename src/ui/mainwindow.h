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
#include "core/usertools.h"

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
class PanelToolbar;
class ViewerWidget;

// Top-level window with two side-by-side panels.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void createCommands();
    void createMenus();
    void fillFavouritesMenu(QMenu *menu);
    void saveFavouritesNow() const;
    void setActivePanel(FilePanel *panel);
    void setShowHidden(bool show);
    void setShowFolderSizes(bool show);
    // Unknown follows Windows. Light and Dark override it for this app only.
    void setTheme(Qt::ColorScheme scheme);
    // Re-applies colours that widgets bake in, after the palette has switched.
    void refreshThemeColours();
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
    // Copies, or moves after a cut, the files on the clipboard into the active panel's folder.
    void cmPaste();
    void transfer(LocalFileOperation::Kind kind, const QStringList &sources, const QString &baseDir,
                  const QString &title, const QString &label, const QString &defaultDestination,
                  bool fromDrop = false);
    // Whether a drag and drop asks before copying or moving. Ticks the View menu entry to match.
    void setConfirmDrop(bool confirm);
    // Whether Delete asks first on local drives. Network paths always ask.
    void setConfirmDelete(bool confirm);
    // Opens the Hot Keys dialog, then applies and saves the result.
    void configHotKeys();
    // Queues op on the active side. Each side runs its own operations one at a time in the background.
    void runOperation(FileOperation *op, const QString &title);
    // Lets every tab showing one of paths, or a folder inside it, release its folder first.
    void releaseFolders(const QStringList &paths);
    void onOperationFinished(const FileOperation::Result &result);
    bool operationsBusy() const;
    void showContextMenu(FilePanel *panel, QWidget *anchor, const QPoint &localPos, bool onItem);
    // Opens the copy or move dialog for files dropped on panel, with targetDir filled in.
    void dropFiles(FilePanel *panel, const QStringList &sources, const QString &targetDir, bool move);
    // Runs an in-place rename from a panel as a move on that panel's side.
    void renameInPlace(FilePanel *panel, const QString &path, const QString &newName);
    // User toolbar buttons, shared by both sides. side is the toolbar the button was pressed on.
    void runUserTool(int index, PanelTabs *side);
    void editUserTool(int index);
    void setUserTools(const UserToolList &tools);

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
    QHash<PanelTabs *, PanelToolbar *> m_toolbars;
    UserToolList m_userTools;
    bool m_showHidden = false;
    bool m_showFolderSizes = false;
    Qt::ColorScheme m_theme = Qt::ColorScheme::Unknown;
    bool m_confirmDrop = true;
    bool m_confirmDelete = true;
};

} // namespace bifrons
