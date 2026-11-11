#include "ui/terminal.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#endif

namespace bifrons {

namespace {

bool startIn(const QString &program, const QStringList &arguments, const QString &dir)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setWorkingDirectory(dir);
#if defined(Q_OS_WIN)
    // A console program started from a GUI app needs its own console window.
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NEW_CONSOLE;
    });
#endif
    return process.startDetached();
}

} // namespace

bool openTerminal(const QString &dir, QString *error)
{
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        if (error != nullptr) {
            *error = QStringLiteral("Folder does not exist: %1").arg(QDir::toNativeSeparators(dir));
        }
        return false;
    }
    const QString native = QDir::toNativeSeparators(dir);

    // Each candidate is tried once. startDetached fails only when the program cannot be launched,
    // so a later candidate never runs after an earlier one has started.
#if defined(Q_OS_WIN)
    if (startIn(QStringLiteral("wt.exe"), {QStringLiteral("-d"), native}, dir)) {
        return true;
    }
    // cmd cannot use a network folder as its working directory, so pushd maps it to a drive letter.
    const bool unc = native.startsWith(QLatin1String("\\\\"));
    if (unc ? startIn(QStringLiteral("cmd.exe"), {QStringLiteral("/k"), QStringLiteral("pushd"), native},
                      QDir::homePath())
            : startIn(QStringLiteral("cmd.exe"), {}, dir)) {
        return true;
    }
#elif defined(Q_OS_MACOS)
    if (startIn(QStringLiteral("open"), {QStringLiteral("-a"), QStringLiteral("Terminal"), native}, dir)) {
        return true;
    }
#else
    QStringList candidates;
    const QString preferred = qEnvironmentVariable("TERMINAL");
    if (!preferred.isEmpty()) {
        candidates.append(preferred);
    }
    candidates << QStringLiteral("x-terminal-emulator") << QStringLiteral("gnome-terminal")
               << QStringLiteral("konsole") << QStringLiteral("xfce4-terminal") << QStringLiteral("xterm");
    for (const QString &program : std::as_const(candidates)) {
        if (startIn(program, {}, dir)) {
            return true;
        }
    }
#endif
    if (error != nullptr) {
        *error = QStringLiteral("No terminal program could be started.");
    }
    return false;
}

} // namespace bifrons
