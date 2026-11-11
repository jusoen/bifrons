#include "core/usertools.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>

#include <algorithm>

namespace bifrons {

namespace {

constexpr int MaxTools = 100;

QString expandOne(const QString &arg, const ToolContext &context)
{
    QString out;
    out.reserve(arg.size());
    for (qsizetype i = 0; i < arg.size(); ++i) {
        const QChar c = arg.at(i);
        if (c != QLatin1Char('%') || i + 1 >= arg.size()) {
            out += c;
            continue;
        }
        const QChar code = arg.at(++i);
        switch (code.unicode()) {
        case 'd':
            out += QDir::toNativeSeparators(context.currentDir);
            break;
        case 'p':
            out += QDir::toNativeSeparators(context.currentPath);
            break;
        case 'n':
            out += QFileInfo(context.currentPath).fileName();
            break;
        case 't':
            out += QDir::toNativeSeparators(context.targetDir);
            break;
        case 's': {
            QStringList quoted;
            for (const QString &path : context.selectedPaths) {
                quoted.append(QLatin1Char('"') + QDir::toNativeSeparators(path) + QLatin1Char('"'));
            }
            out += quoted.join(QLatin1Char(' '));
            break;
        }
        case '%':
            out += QLatin1Char('%');
            break;
        default:
            // Unknown code: keep it as typed.
            out += c;
            out += code;
            break;
        }
    }
    return out;
}

} // namespace

UserToolList loadUserTools(QSettings &s)
{
    UserToolList tools;
    const int count = std::min(s.beginReadArray(QStringLiteral("userTools")), MaxTools);
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        UserTool tool;
        tool.name = s.value(QStringLiteral("name")).toString();
        tool.program = s.value(QStringLiteral("program")).toString();
        tool.arguments = s.value(QStringLiteral("arguments")).toString();
        tool.icon = s.value(QStringLiteral("icon")).toString();
        if (!tool.program.isEmpty()) {
            tools.append(tool);
        }
    }
    s.endArray();
    return tools;
}

void saveUserTools(QSettings &s, const UserToolList &tools)
{
    s.remove(QStringLiteral("userTools"));
    s.beginWriteArray(QStringLiteral("userTools"), static_cast<int>(std::min<qsizetype>(tools.size(), MaxTools)));
    for (int i = 0; i < tools.size() && i < MaxTools; ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("name"), tools.at(i).name);
        s.setValue(QStringLiteral("program"), tools.at(i).program);
        s.setValue(QStringLiteral("arguments"), tools.at(i).arguments);
        s.setValue(QStringLiteral("icon"), tools.at(i).icon);
    }
    s.endArray();
}

QStringList expandToolArguments(const QString &argumentTemplate, const ToolContext &context)
{
    QStringList args;
    for (const QString &arg : QProcess::splitCommand(argumentTemplate)) {
        if (arg == QLatin1String("%s")) {
            for (const QString &path : context.selectedPaths) {
                args.append(QDir::toNativeSeparators(path));
            }
        } else {
            args.append(expandOne(arg, context));
        }
    }
    return args;
}

} // namespace bifrons
