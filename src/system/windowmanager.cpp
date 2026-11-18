/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "windowmanager.h"

#include <QDateTime>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

namespace Kity {

WindowManager &WindowManager::instance()
{
    static WindowManager s_instance;
    return s_instance;
}

WindowManager::WindowManager(QObject *parent)
    : QObject(parent)
{
}

QString WindowManager::normalizeQuery(const QString &input)
{
    QString q = input.trimmed().toLower();
    q.remove(QRegularExpression(QStringLiteral("[.!?,;¿¡]+$")));
    q.remove(QRegularExpression(QStringLiteral("^(?:el|la|los|las|un|una|unos|unas|the|a|an)\\s+")));
    return q.trimmed();
}

bool WindowManager::runKWinScript(const QString &jsCode, QString *scriptOutputOut) const
{
    const qint64 token = QDateTime::currentMSecsSinceEpoch();
    const QString scriptName = QStringLiteral("kity_kwin_%1").arg(token);
    const QString scriptPath = QDir::tempPath() + QStringLiteral("/%1.js").arg(scriptName);

    QFile file(scriptPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    file.write(jsCode.toUtf8());
    file.close();

    QDBusInterface scriptingIface(QStringLiteral("org.kde.KWin"),
                                  QStringLiteral("/Scripting"),
                                  QStringLiteral("org.kde.kwin.Scripting"),
                                  QDBusConnection::sessionBus());
    if (!scriptingIface.isValid()) {
        QFile::remove(scriptPath);
        return false;
    }

    QDBusReply<int> loadReply = scriptingIface.call(QStringLiteral("loadScript"), scriptPath, scriptName);
    if (!loadReply.isValid()) {
        QFile::remove(scriptPath);
        return false;
    }

    const int scriptId = loadReply.value();
    QDBusInterface scriptObj(QStringLiteral("org.kde.KWin"),
                             QStringLiteral("/Scripting/Script%1").arg(scriptId),
                             QStringLiteral("org.kde.kwin.Script"),
                             QDBusConnection::sessionBus());
    if (scriptObj.isValid()) {
        scriptObj.call(QStringLiteral("run"));
    } else {
        scriptingIface.call(QStringLiteral("start"));
    }
    scriptingIface.call(QStringLiteral("unloadScript"), scriptName);
    QFile::remove(scriptPath);

    if (scriptOutputOut) {
        // Read recent journald output from plasma-kwin_wayland
        QEventLoop loop;
        QTimer::singleShot(80, &loop, &QEventLoop::quit);
        loop.exec();

        QProcess jProc;
        jProc.start(QStringLiteral("journalctl"),
                    {QStringLiteral("--user"), QStringLiteral("-u"), QStringLiteral("plasma-kwin_wayland.service"),
                     QStringLiteral("-n"), QStringLiteral("40"), QStringLiteral("--no-pager")});
        if (jProc.waitForFinished(1000)) {
            *scriptOutputOut = QString::fromUtf8(jProc.readAllStandardOutput());
        }
    }

    return true;
}

QList<WindowInfo> WindowManager::listWindows() const
{
    QList<WindowInfo> result;
    const qint64 token = QDateTime::currentMSecsSinceEpoch();
    const QString marker = QStringLiteral("KITY_WINLIST_%1:").arg(token);

    const QString jsCode = QStringLiteral(
        "const wins = workspace.windowList();\n"
        "for (let i = 0; i < wins.length; ++i) {\n"
        "    const w = wins[i];\n"
        "    if (w.normalWindow && !w.skipTaskbar) {\n"
        "        print(\"%1\" + (w.resourceClass || '') + \"|||\" + (w.caption || '') + \"|||\" + (w.active ? \"1\" : \"0\") + \"|||\" + (w.minimized ? \"1\" : \"0\"));\n"
        "    }\n"
        "}\n").arg(marker);

    QString logs;
    if (!runKWinScript(jsCode, &logs)) {
        return result;
    }

    const QStringList lines = logs.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int idx = line.indexOf(marker);
        if (idx < 0) continue;

        const QString payload = line.mid(idx + marker.length()).trimmed();
        const QStringList parts = payload.split(QStringLiteral("|||"));
        if (parts.size() < 4) continue;

        WindowInfo info;
        info.resourceClass = parts.at(0).trimmed();
        info.caption = parts.at(1).trimmed();
        info.active = (parts.at(2) == QStringLiteral("1"));
        info.minimized = (parts.at(3) == QStringLiteral("1"));

        if (!info.resourceClass.contains(QStringLiteral("plasmashell"), Qt::CaseInsensitive)) {
            result.append(info);
        }
    }

    return result;
}

QString WindowManager::windowsSummary() const
{
    const QList<WindowInfo> wins = listWindows();
    if (wins.isEmpty()) {
        return QString();
    }

    QStringList lines;
    for (const auto &w : wins) {
        QString line = QStringLiteral("- App: `%1` | Título: \"%2\"").arg(w.resourceClass, w.caption);
        if (w.active) {
            line += QStringLiteral(" [ACTIVA / ENFOCADA]");
        } else if (w.minimized) {
            line += QStringLiteral(" [MINIMIZADA]");
        }
        lines.append(line);
    }
    return lines.join(QLatin1Char('\n'));
}

bool WindowManager::closeWindow(const QString &query, QString *closedCaptionOut)
{
    const QString target = normalizeQuery(query);
    if (target.isEmpty()) return false;

    const qint64 token = QDateTime::currentMSecsSinceEpoch();
    const QString marker = QStringLiteral("KITY_CLOSE_%1:").arg(token);

    // Escape special characters in JS string literal
    QString safeTarget = target;
    safeTarget.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));

    const QString jsCode = QStringLiteral(
        "const wins = workspace.windowList();\n"
        "const target = \"%1\";\n"
        "let found = false;\n"
        "for (let i = 0; i < wins.length; ++i) {\n"
        "    const w = wins[i];\n"
        "    if (w.normalWindow) {\n"
        "        const cap = (w.caption || '').toLowerCase();\n"
        "        const res = (w.resourceClass || '').toLowerCase();\n"
        "        if (cap.indexOf(target) !== -1 || res.indexOf(target) !== -1 || target.indexOf(res) !== -1) {\n"
        "            print(\"%2SUCCESS:\" + (w.caption || w.resourceClass));\n"
        "            w.closeWindow();\n"
        "            found = true;\n"
        "            break;\n"
        "        }\n"
        "    }\n"
        "}\n"
        "if (!found) {\n"
        "    print(\"%2NOT_FOUND\");\n"
        "}\n").arg(safeTarget, marker);

    QString logs;
    if (!runKWinScript(jsCode, &logs)) {
        return false;
    }

    const QString prefix = marker + QStringLiteral("SUCCESS:");
    const int idx = logs.indexOf(prefix);
    if (idx >= 0) {
        int endIdx = logs.indexOf(QLatin1Char('\n'), idx);
        if (endIdx < 0) endIdx = logs.length();
        const QString foundTitle = logs.mid(idx + prefix.length(), endIdx - (idx + prefix.length())).trimmed();
        if (closedCaptionOut) {
            *closedCaptionOut = foundTitle.isEmpty() ? query : foundTitle;
        }
        return true;
    }

    return false;
}

bool WindowManager::activateWindow(const QString &query, QString *activatedCaptionOut)
{
    const QString target = normalizeQuery(query);
    if (target.isEmpty()) return false;

    const qint64 token = QDateTime::currentMSecsSinceEpoch();
    const QString marker = QStringLiteral("KITY_ACTIVATE_%1:").arg(token);

    QString safeTarget = target;
    safeTarget.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));

    const QString jsCode = QStringLiteral(
        "const wins = workspace.windowList();\n"
        "const target = \"%1\";\n"
        "let found = false;\n"
        "for (let i = 0; i < wins.length; ++i) {\n"
        "    const w = wins[i];\n"
        "    if (w.normalWindow) {\n"
        "        const cap = (w.caption || '').toLowerCase();\n"
        "        const res = (w.resourceClass || '').toLowerCase();\n"
        "        if (cap.indexOf(target) !== -1 || res.indexOf(target) !== -1 || target.indexOf(res) !== -1) {\n"
        "            if (w.minimized) w.minimized = false;\n"
        "            workspace.activeWindow = w;\n"
        "            print(\"%2SUCCESS:\" + (w.caption || w.resourceClass));\n"
        "            found = true;\n"
        "            break;\n"
        "        }\n"
        "    }\n"
        "}\n"
        "if (!found) {\n"
        "    print(\"%2NOT_FOUND\");\n"
        "}\n").arg(safeTarget, marker);

    QString logs;
    if (!runKWinScript(jsCode, &logs)) {
        return false;
    }

    const QString prefix = marker + QStringLiteral("SUCCESS:");
    const int idx = logs.indexOf(prefix);
    if (idx >= 0) {
        int endIdx = logs.indexOf(QLatin1Char('\n'), idx);
        if (endIdx < 0) endIdx = logs.length();
        const QString foundTitle = logs.mid(idx + prefix.length(), endIdx - (idx + prefix.length())).trimmed();
        if (activatedCaptionOut) {
            *activatedCaptionOut = foundTitle.isEmpty() ? query : foundTitle;
        }
        return true;
    }

    return false;
}

bool WindowManager::minimizeWindow(const QString &query)
{
    const QString target = normalizeQuery(query);
    const QString jsCode = target.isEmpty() ?
        QStringLiteral("if (workspace.activeWindow) { workspace.activeWindow.minimized = true; }") :
        QStringLiteral(
            "const wins = workspace.windowList();\n"
            "const target = \"%1\";\n"
            "for (let i = 0; i < wins.length; ++i) {\n"
            "    const w = wins[i];\n"
            "    if (w.normalWindow) {\n"
            "        const cap = (w.caption || '').toLowerCase();\n"
            "        const res = (w.resourceClass || '').toLowerCase();\n"
            "        if (cap.indexOf(target) !== -1 || res.indexOf(target) !== -1) {\n"
            "            w.minimized = true;\n"
            "            break;\n"
            "        }\n"
            "    }\n"
            "}\n").arg(target);

    return runKWinScript(jsCode);
}

bool WindowManager::maximizeWindow(const QString &query)
{
    const QString target = normalizeQuery(query);
    const QString jsCode = target.isEmpty() ?
        QStringLiteral("if (workspace.activeWindow) { workspace.activeWindow.maximized = !workspace.activeWindow.maximized; }") :
        QStringLiteral(
            "const wins = workspace.windowList();\n"
            "const target = \"%1\";\n"
            "for (let i = 0; i < wins.length; ++i) {\n"
            "    const w = wins[i];\n"
            "    if (w.normalWindow) {\n"
            "        const cap = (w.caption || '').toLowerCase();\n"
            "        const res = (w.resourceClass || '').toLowerCase();\n"
            "        if (cap.indexOf(target) !== -1 || res.indexOf(target) !== -1) {\n"
            "            w.maximized = !w.maximized;\n"
            "            break;\n"
            "        }\n"
            "    }\n"
            "}\n").arg(target);

    return runKWinScript(jsCode);
}

bool WindowManager::showDesktop()
{
    return runKWinScript(QStringLiteral("workspace.showingDesktop = !workspace.showingDesktop;"));
}

} // namespace Kity
