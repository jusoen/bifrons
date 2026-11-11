#include "ui/terminal.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#include <shellapi.h>

#include <string>
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

#if defined(Q_OS_WIN)
namespace {

// Quotes one argument for a Windows command line when it needs it.
QString quoteArgument(const QString &arg)
{
    if (!arg.isEmpty() && !arg.contains(QLatin1Char(' ')) && !arg.contains(QLatin1Char('\t'))
        && !arg.contains(QLatin1Char('"')) && !arg.contains(QLatin1Char('&'))) {
        return arg;
    }
    QString inner = arg;
    inner.replace(QLatin1Char('"'), QLatin1String("\\\""));
    return QLatin1Char('"') + inner + QLatin1Char('"');
}

QString joinArguments(const QStringList &args)
{
    QStringList quoted;
    for (const QString &arg : args) {
        quoted.append(quoteArgument(arg));
    }
    return quoted.join(QLatin1Char(' '));
}

} // namespace
#endif

bool launchProgram(const QString &program, const QStringList &arguments, const QString &dir, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (program.trimmed().isEmpty()) {
        return fail(QStringLiteral("No program is set for this button."));
    }
    QString resolved = program.trimmed();
    if (!resolved.contains(QLatin1Char('/')) && !resolved.contains(QLatin1Char('\\'))) {
        const QString onPath = QStandardPaths::findExecutable(resolved);
        if (!onPath.isEmpty()) {
            resolved = onPath;
        }
    }
    if (!QFileInfo::exists(resolved)) {
        return fail(QStringLiteral("Program not found: %1").arg(QDir::toNativeSeparators(program)));
    }
    const QString native = QDir::toNativeSeparators(resolved);
    const QString suffix = QFileInfo(resolved).suffix().toLower();

#if defined(Q_OS_WIN)
    if (suffix == QLatin1String("exe") || suffix == QLatin1String("com")) {
        if (startIn(native, arguments, dir)) {
            return true;
        }
        return fail(QStringLiteral("Could not start %1").arg(native));
    }
    if (suffix == QLatin1String("bat") || suffix == QLatin1String("cmd")) {
        // cmd /c strips one pair of outer quotes, so /s with the whole line wrapped keeps every quote intact.
        QProcess process;
        process.setProgram(QStringLiteral("cmd.exe"));
        process.setNativeArguments(QStringLiteral("/s /c \"%1\"").arg(joinArguments(QStringList{native} + arguments)));
        process.setWorkingDirectory(dir);
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
            args->flags |= CREATE_NEW_CONSOLE;
        });
        if (process.startDetached()) {
            return true;
        }
        return fail(QStringLiteral("Could not run %1").arg(native));
    }
    if (suffix == QLatin1String("ps1")) {
        const QStringList psArgs = QStringList{QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                                               QStringLiteral("Bypass"), QStringLiteral("-File"), native}
            + arguments;
        if (startIn(QStringLiteral("powershell.exe"), psArgs, dir)) {
            return true;
        }
        return fail(QStringLiteral("Could not run %1").arg(native));
    }
    // Anything else (Python, VBScript, documents) opens through its file association.
    const std::wstring file = native.toStdWString();
    const std::wstring params = joinArguments(arguments).toStdWString();
    const std::wstring workDir = QDir::toNativeSeparators(dir).toStdWString();
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"open";
    info.lpFile = file.c_str();
    info.lpParameters = params.empty() ? nullptr : params.c_str();
    info.lpDirectory = workDir.empty() ? nullptr : workDir.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&info) != FALSE) {
        return true;
    }
    return fail(QStringLiteral("No program is associated with %1 files.").arg(suffix.isEmpty() ? native : suffix));
#else
    Q_UNUSED(suffix);
    if (startIn(native, arguments, dir)) {
        return true;
    }
    return fail(QStringLiteral("Could not start %1").arg(native));
#endif
}

} // namespace bifrons
