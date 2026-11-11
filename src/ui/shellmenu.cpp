#include "ui/shellmenu.h"

#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QWidget>

#include <cmath>
#include <string>
#include <vector>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace bifrons {

void showEntryMenu(QWidget *anchor, const QPoint &localPos, const QList<MenuEntry> &entries)
{
    if (anchor == nullptr || entries.isEmpty()) {
        return;
    }
    QMenu menu(anchor);
    for (const MenuEntry &entry : entries) {
        if (entry.text.isEmpty()) {
            menu.addSeparator();
        } else {
            QAction *action = menu.addAction(entry.text);
            const std::function<void()> run = entry.action;
            QObject::connect(action, &QAction::triggered, anchor, [run] {
                if (run) {
                    run();
                }
            });
        }
    }
    menu.exec(anchor->mapToGlobal(localPos));
}

#if defined(Q_OS_WIN)

namespace {

// Command ids. Bifrons entries sit below the range handed to the shell.
constexpr UINT EntryFirstId = 1;
constexpr UINT ShellFirstId = 0x1000;
constexpr UINT ShellLastId = 0x7FFF;
constexpr UINT_PTR SubclassId = 0xB1F0;

// Submenus such as Send to and Open with draw themselves through these while the menu is open.
// Only one context menu can be open at a time, so plain statics are enough. UI thread only.
IContextMenu2 *s_menu2 = nullptr;
IContextMenu3 *s_menu3 = nullptr;

LRESULT CALLBACK forwardMenuMessages(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    switch (msg) {
    case WM_INITMENUPOPUP:
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
    case WM_MENUCHAR:
        if (s_menu3 != nullptr) {
            LRESULT result = 0;
            if (SUCCEEDED(s_menu3->HandleMenuMsg2(msg, wp, lp, &result))) {
                return result;
            }
        } else if (s_menu2 != nullptr && SUCCEEDED(s_menu2->HandleMenuMsg(msg, wp, lp))) {
            return msg == WM_INITMENUPOPUP ? 0 : TRUE;
        }
        break;
    default:
        break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// Frees the PIDLs it holds when it goes out of scope.
struct PidlList
{
    std::vector<PIDLIST_ABSOLUTE> items;
    ~PidlList()
    {
        for (PIDLIST_ABSOLUTE pidl : items) {
            CoTaskMemFree(pidl);
        }
    }
};

bool sameFolder(const QStringList &paths)
{
    const QString first = QFileInfo(paths.first()).absolutePath();
    for (const QString &path : paths) {
        if (QFileInfo(path).absolutePath().compare(first, Qt::CaseInsensitive) != 0) {
            return false;
        }
    }
    return true;
}

} // namespace

bool showShellContextMenu(QWidget *anchor, const QPoint &localPos, const QStringList &paths,
                          const QList<MenuEntry> &entries, const std::function<void()> &onDelete)
{
    if (anchor == nullptr || paths.isEmpty() || !sameFolder(paths)) {
        return false;
    }
    QWidget *top = anchor->window();
    const HWND hwnd = reinterpret_cast<HWND>(top->winId());

    PidlList pidls;
    for (const QString &path : paths) {
        const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (FAILED(SHParseDisplayName(native.c_str(), nullptr, &pidl, 0, nullptr)) || pidl == nullptr) {
            return false;
        }
        pidls.items.push_back(pidl);
    }
    IShellFolder *parent = nullptr;
    PCUITEMID_CHILD firstChild = nullptr;
    if (FAILED(SHBindToParent(pidls.items.front(), IID_PPV_ARGS(&parent), &firstChild)) || parent == nullptr) {
        return false;
    }
    std::vector<PCUITEMID_CHILD> children;
    for (PIDLIST_ABSOLUTE pidl : pidls.items) {
        children.push_back(ILFindLastID(pidl));
    }
    IContextMenu *menu = nullptr;
    const HRESULT got = parent->GetUIObjectOf(hwnd, static_cast<UINT>(children.size()), children.data(),
                                              IID_IContextMenu, nullptr, reinterpret_cast<void **>(&menu));
    parent->Release();
    if (FAILED(got) || menu == nullptr) {
        return false;
    }

    HMENU popup = CreatePopupMenu();
    if (popup == nullptr) {
        menu->Release();
        return false;
    }
    for (int i = 0; i < entries.size(); ++i) {
        const MenuEntry &entry = entries.at(i);
        if (entry.text.isEmpty()) {
            (void)AppendMenuW(popup, MF_SEPARATOR, 0, nullptr); // A missing separator is cosmetic.
        } else {
            const std::wstring text = entry.text.toStdWString();
            (void)AppendMenuW(popup, MF_STRING, EntryFirstId + static_cast<UINT>(i), text.c_str());
        }
    }
    if (!entries.isEmpty()) {
        (void)AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    }
    const bool shift = GetKeyState(VK_SHIFT) < 0;
    // Shift shows the extended verbs, as in Explorer. Rename is left out: Bifrons renames itself.
    const UINT flags = CMF_NORMAL | CMF_EXPLORE | (shift ? CMF_EXTENDEDVERBS : 0);
    if (FAILED(menu->QueryContextMenu(popup, static_cast<UINT>(GetMenuItemCount(popup)), ShellFirstId, ShellLastId,
                                      flags))) {
        (void)DestroyMenu(popup);
        menu->Release();
        return false;
    }

    (void)menu->QueryInterface(IID_PPV_ARGS(&s_menu3)); // Optional: older handlers lack these.
    if (s_menu3 == nullptr) {
        (void)menu->QueryInterface(IID_PPV_ARGS(&s_menu2));
    }
    const bool subclassed = SetWindowSubclass(hwnd, forwardMenuMessages, SubclassId, 0) != FALSE;

    // Win32 wants physical screen pixels: scale within the window, then let Windows place the window.
    const QPoint inWindow = anchor->mapTo(top, localPos);
    const qreal dpr = top->devicePixelRatioF();
    POINT pt = {static_cast<LONG>(std::lround(inWindow.x() * dpr)), static_cast<LONG>(std::lround(inWindow.y() * dpr))};
    (void)ClientToScreen(hwnd, &pt); // Cannot fail for a live window. The menu would only be misplaced.
    const UINT chosen = static_cast<UINT>(TrackPopupMenuEx(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, hwnd,
                                                           nullptr));

    if (subclassed) {
        (void)RemoveWindowSubclass(hwnd, forwardMenuMessages, SubclassId); // Fails only if never added.
    }
    if (chosen >= ShellFirstId && chosen <= ShellLastId) {
        const UINT offset = chosen - ShellFirstId;
        wchar_t verb[64] = {};
        const bool hasVerb = SUCCEEDED(menu->GetCommandString(offset, GCS_VERBW, nullptr,
                                                               reinterpret_cast<LPSTR>(verb), 64));
        if (hasVerb && _wcsicmp(verb, L"delete") == 0 && onDelete) {
            onDelete();
        } else {
            CMINVOKECOMMANDINFOEX info = {};
            info.cbSize = sizeof(info);
            info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
            if (GetKeyState(VK_CONTROL) < 0) {
                info.fMask |= CMIC_MASK_CONTROL_DOWN;
            }
            if (shift) {
                info.fMask |= CMIC_MASK_SHIFT_DOWN;
            }
            info.hwnd = hwnd;
            info.lpVerb = MAKEINTRESOURCEA(offset);
            info.lpVerbW = MAKEINTRESOURCEW(offset);
            info.nShow = SW_SHOWNORMAL;
            info.ptInvoke = pt;
            // A failed or cancelled verb has already told the user, if it was going to.
            (void)menu->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info));
        }
    }

    if (s_menu3 != nullptr) {
        s_menu3->Release();
        s_menu3 = nullptr;
    }
    if (s_menu2 != nullptr) {
        s_menu2->Release();
        s_menu2 = nullptr;
    }
    menu->Release();
    (void)DestroyMenu(popup); // Freed with the process if this fails.

    // Bifrons entries run last, after the shell objects are released.
    if (chosen >= EntryFirstId && chosen < EntryFirstId + static_cast<UINT>(entries.size())) {
        const MenuEntry &entry = entries.at(static_cast<int>(chosen - EntryFirstId));
        if (entry.action) {
            entry.action();
        }
    }
    return true;
}

#else

bool showShellContextMenu(QWidget *, const QPoint &, const QStringList &, const QList<MenuEntry> &,
                          const std::function<void()> &)
{
    return false;
}

#endif

} // namespace bifrons
