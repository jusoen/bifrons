#pragma once

#include <QList>
#include <QPoint>
#include <QString>
#include <QStringList>

#include <functional>

class QWidget;

namespace bifrons {

// One Bifrons entry at the top of a context menu. An empty text is a separator.
struct MenuEntry
{
    QString text;
    std::function<void()> action;
};

// Shows Explorer's context menu for paths at localPos in anchor, with entries above the shell items.
// The shell's own Delete runs onDelete instead, so deletes go through Bifrons' background queue.
// Returns false without showing anything when the shell menu is unavailable (not Windows, items in
// different folders, or a path the shell cannot resolve). The caller then shows its own menu.
bool showShellContextMenu(QWidget *anchor, const QPoint &localPos, const QStringList &paths,
                          const QList<MenuEntry> &entries, const std::function<void()> &onDelete);

// Plain Qt menu with entries, for when the shell menu is unavailable or not wanted.
void showEntryMenu(QWidget *anchor, const QPoint &localPos, const QList<MenuEntry> &entries);

} // namespace bifrons
