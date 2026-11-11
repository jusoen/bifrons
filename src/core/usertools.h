#pragma once

#include <QList>
#include <QString>
#include <QStringList>

class QSettings;

namespace bifrons {

// A user-defined toolbar button that runs a program or script.
struct UserTool
{
    QString name;
    // Program or script to run. A bare name is looked up on PATH.
    QString program;
    // Argument template, split like a command line before placeholders are filled in (see below).
    QString arguments;
    // Optional icon file. Empty uses the program's own icon.
    QString icon;
};

using UserToolList = QList<UserTool>;

UserToolList loadUserTools(QSettings &s);
void saveUserTools(QSettings &s, const UserToolList &tools);

// What the placeholders refer to when a button is pressed. Paths use '/' separators.
struct ToolContext
{
    QString currentDir;
    // Entry under the cursor, or empty on the ".." row.
    QString currentPath;
    // Marked entries, or the entry under the cursor when none are marked.
    QStringList selectedPaths;
    // Folder shown in the other pane.
    QString targetDir;
};

// Placeholders, expanded with native separators:
//   %d current folder      %p entry under the cursor     %n its file name
//   %s selected entries    %t other pane's folder        %% a literal %
// The template is split into arguments first, so expanded paths with spaces stay one argument.
// An argument that is exactly %s becomes one argument per selected entry. Inside a longer
// argument, %s becomes the entries joined by spaces, each quoted.
QStringList expandToolArguments(const QString &argumentTemplate, const ToolContext &context);

} // namespace bifrons
