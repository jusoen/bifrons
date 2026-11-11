#pragma once

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

namespace dcpp {

class CommandRegistry;
class FilePanel;
class FindDialog;
class LocalFileSource;
class OperationRunner;
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
    QStackedWidget *m_leftStack = nullptr;
    QStackedWidget *m_rightStack = nullptr;
    // Side currently replaced by the quick view, or nullptr when it is off.
    QStackedWidget *m_quickSide = nullptr;
    ViewerWidget *m_quickView = nullptr;
    QTimer m_quickTimer;
    QList<RenameStep> m_lastRename;
    FavouriteList m_favourites;
    bool m_closePending = false;
    bool m_showHidden = false;
};

} // namespace dcpp
