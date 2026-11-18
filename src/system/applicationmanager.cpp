/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "applicationmanager.h"
#include "windowmanager.h"

#include <KService>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace Kity {

ApplicationManager &ApplicationManager::instance()
{
    static ApplicationManager s_instance;
    return s_instance;
}

ApplicationManager::ApplicationManager(QObject *parent)
    : QObject(parent)
{
    reloadCatalog();
}

QString ApplicationManager::normalizeQuery(const QString &input)
{
    QString q = input.trimmed().toLower();
    q.remove(QRegularExpression(QStringLiteral("[.!?,;¿¡]+$")));
    q.remove(QRegularExpression(QStringLiteral("^(?:el|la|los|las|un|una|unos|unas|the|a|an)\\s+")));
    return q.trimmed();
}

void ApplicationManager::reloadCatalog()
{
    m_catalog.clear();
    const auto services = KService::allServices();
    for (const auto &svc : services) {
        if (!svc || svc->noDisplay()) continue;

        AppInfo info;
        info.id = svc->entryPath();
        info.name = svc->name();
        info.genericName = svc->genericName();
        info.exec = svc->exec();
        info.desktopEntryName = svc->desktopEntryName();
        info.icon = svc->icon();

        m_catalog.append(info);
    }
}

QList<AppInfo> ApplicationManager::searchApps(const QString &query) const
{
    const QString clean = normalizeQuery(query);
    if (clean.isEmpty()) return {};

    QList<AppInfo> exactMatches;
    QList<AppInfo> partialMatches;

    for (const auto &app : m_catalog) {
        const QString nameNorm = normalizeQuery(app.name);
        const QString entryNorm = normalizeQuery(app.desktopEntryName);
        const QString genericNorm = normalizeQuery(app.genericName);

        if (nameNorm == clean || entryNorm == clean) {
            exactMatches.append(app);
        } else if (nameNorm.contains(clean) || entryNorm.contains(clean) || genericNorm.contains(clean)) {
            partialMatches.append(app);
        }
    }

    if (!exactMatches.isEmpty()) return exactMatches;
    return partialMatches;
}

bool ApplicationManager::resolveSemanticAlias(const QString &clean, QString *launchedNameOut)
{
    // 1. Terminal Emulator
    static const QRegularExpression termRegex(
        QStringLiteral("^(?:terminal|consola|kterminal|konsole|shell|linea\\s+de\\s+comandos)$"),
        QRegularExpression::CaseInsensitiveOption);
    if (clean.contains(termRegex)) {
        if (launchedNameOut) *launchedNameOut = QStringLiteral("la terminal");
        if (QProcess::startDetached(QStringLiteral("konsole"), {})) return true;
        return QProcess::startDetached(QStringLiteral("gtk-launch"), {QStringLiteral("org.kde.konsole")});
    }

    // 2. Web Browser
    static const QRegularExpression browserRegex(
        QStringLiteral("^(?:navegador|navegador\\s+web|internet|browser|web)$"),
        QRegularExpression::CaseInsensitiveOption);
    if (clean.contains(browserRegex)) {
        if (launchedNameOut) *launchedNameOut = QStringLiteral("el navegador web");
        QProcess proc;
        proc.start(QStringLiteral("xdg-settings"), {QStringLiteral("get"), QStringLiteral("default-web-browser")});
        if (proc.waitForFinished(1000) && proc.exitCode() == 0) {
            QString desktop = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
            if (!desktop.isEmpty()) {
                if (desktop.endsWith(QStringLiteral(".desktop"))) desktop.chop(8);
                if (QProcess::startDetached(QStringLiteral("gtk-launch"), {desktop})) return true;
            }
        }
        return QProcess::startDetached(QStringLiteral("xdg-open"), {QStringLiteral("https://")});
    }

    // 3. File Manager
    static const QRegularExpression fmRegex(
        QStringLiteral("^(?:archivos|mis\\s+archivos|gestor\\s+de\\s+archivos|carpetas|carpeta\\s+personal|dolphin)$"),
        QRegularExpression::CaseInsensitiveOption);
    if (clean.contains(fmRegex)) {
        if (launchedNameOut) *launchedNameOut = QStringLiteral("el gestor de archivos");
        return QProcess::startDetached(QStringLiteral("dolphin"), {});
    }

    // 4. System Settings
    static const QRegularExpression settingsRegex(
        QStringLiteral("^(?:ajustes|configuraci[oó]n|preferencias|preferencias\\s+del\\s+sistema|systemsettings)$"),
        QRegularExpression::CaseInsensitiveOption);
    if (clean.contains(settingsRegex)) {
        if (launchedNameOut) *launchedNameOut = QStringLiteral("las Preferencias del Sistema");
        return QProcess::startDetached(QStringLiteral("systemsettings"), {});
    }

    // 5. Calculator
    static const QRegularExpression calcRegex(
        QStringLiteral("^(?:calculadora|kcalc)$"),
        QRegularExpression::CaseInsensitiveOption);
    if (clean.contains(calcRegex)) {
        if (launchedNameOut) *launchedNameOut = QStringLiteral("la calculadora");
        return QProcess::startDetached(QStringLiteral("kcalc"), {});
    }

    // 6. System Monitor
    static const QRegularExpression monitorRegex(
        QStringLiteral("^(?:monitor\\s+del\\s+sistema|administrador\\s+de\\s+tareas|procesos|recursos)$"),
        QRegularExpression::CaseInsensitiveOption);
    if (clean.contains(monitorRegex)) {
        if (launchedNameOut) *launchedNameOut = QStringLiteral("el Monitor del Sistema");
        if (QProcess::startDetached(QStringLiteral("plasma-systemmonitor"), {})) return true;
        return QProcess::startDetached(QStringLiteral("ksysguard"), {});
    }

    // 7. Text Editor
    static const QRegularExpression editorRegex(
        QStringLiteral("^(?:editor|editor\\s+de\\s+texto|notas|kwrite|kate)$"),
        QRegularExpression::CaseInsensitiveOption);
    if (clean.contains(editorRegex)) {
        if (launchedNameOut) *launchedNameOut = QStringLiteral("el editor de texto");
        if (QProcess::startDetached(QStringLiteral("kwrite"), {})) return true;
        return QProcess::startDetached(QStringLiteral("kate"), {});
    }

    // 8. Software Center / Discover
    static const QRegularExpression discoverRegex(
        QStringLiteral("^(?:tienda|tienda\\s+de\\s+aplicaciones|software|descubrir|discover)$"),
        QRegularExpression::CaseInsensitiveOption);
    if (clean.contains(discoverRegex)) {
        if (launchedNameOut) *launchedNameOut = QStringLiteral("Discover");
        return QProcess::startDetached(QStringLiteral("plasma-discover"), {});
    }

    // 9. Screenshot Tool (Spectacle)
    static const QRegularExpression shotRegex(
        QStringLiteral("^(?:captura|captura\\s+de\\s+pantalla|pantallazo|screenshot|spectacle)$"),
        QRegularExpression::CaseInsensitiveOption);
    if (clean.contains(shotRegex)) {
        if (launchedNameOut) *launchedNameOut = QStringLiteral("Spectacle");
        return QProcess::startDetached(QStringLiteral("spectacle"), {});
    }

    return false;
}

bool ApplicationManager::launchApp(const QString &query, QString *launchedNameOut)
{
    const QString clean = normalizeQuery(query);
    if (clean.isEmpty()) return false;

    // Step 1: Check if already open in KWin Wayland and bring to front (Android/Gemini style)
    QString activatedTitle;
    if (WindowManager::instance().activateWindow(clean, &activatedTitle)) {
        if (launchedNameOut) {
            *launchedNameOut = QStringLiteral("%1 (ya estaba abierta, traída al frente)").arg(activatedTitle);
        }
        return true;
    }

    // Step 2: Check semantic aliases
    if (resolveSemanticAlias(clean, launchedNameOut)) {
        return true;
    }

    // Step 3: Search in installed desktop applications catalog
    const auto matches = searchApps(clean);
    if (!matches.isEmpty()) {
        const auto &best = matches.first();
        if (launchedNameOut) *launchedNameOut = best.name;

        if (!best.desktopEntryName.isEmpty()) {
            if (QProcess::startDetached(QStringLiteral("gtk-launch"), {best.desktopEntryName})) {
                return true;
            }
        }

        QString cleanExec = best.exec;
        cleanExec.remove(QRegularExpression(QStringLiteral("%[a-zA-Z]")));
        cleanExec = cleanExec.trimmed();
        QStringList parts = QProcess::splitCommand(cleanExec);
        if (!parts.isEmpty()) {
            const QString prog = parts.takeFirst();
            if (QProcess::startDetached(prog, parts)) {
                return true;
            }
        }
    }

    // Step 4: Fallback - try executing directly if executable exists in PATH
    QStringList directParts = QProcess::splitCommand(clean);
    if (!directParts.isEmpty()) {
        const QString prog = directParts.takeFirst();
        if (!QStandardPaths::findExecutable(prog).isEmpty()) {
            if (QProcess::startDetached(prog, directParts)) {
                if (launchedNameOut) *launchedNameOut = prog;
                return true;
            }
        }
    }

    return false;
}

bool ApplicationManager::closeApp(const QString &query, QString *closedNameOut)
{
    const QString clean = normalizeQuery(query);
    if (clean.isEmpty()) return false;

    // Step 1: Clean Window Close via KWin Wayland Scripting API
    QString closedCaption;
    if (WindowManager::instance().closeWindow(clean, &closedCaption)) {
        if (closedNameOut) *closedNameOut = closedCaption;
        return true;
    }

    // Step 2: Handle aliases if no window was found (e.g. background services or terminal)
    QString targetProc = clean;
    if (clean.contains(QRegularExpression(QStringLiteral("^(?:terminal|consola|konsole)$")))) {
        targetProc = QStringLiteral("konsole");
    } else if (clean.contains(QRegularExpression(QStringLiteral("^(?:navegador|internet|browser)$")))) {
        targetProc = QStringLiteral("brave");
        QProcess::execute(QStringLiteral("pkill"), {QStringLiteral("-f"), QStringLiteral("firefox")});
    } else if (clean.contains(QRegularExpression(QStringLiteral("^(?:archivos|dolphin)$")))) {
        targetProc = QStringLiteral("dolphin");
    } else {
        // Try looking up the executable name in catalog
        const auto matches = searchApps(clean);
        if (!matches.isEmpty()) {
            QString execBin = matches.first().exec.section(QLatin1Char(' '), 0, 0).trimmed();
            if (execBin.contains(QLatin1Char('/'))) {
                execBin = QFileInfo(execBin).fileName();
            }
            if (!execBin.isEmpty()) {
                targetProc = execBin;
            }
        }
    }

    if (closedNameOut) *closedNameOut = query;
    return QProcess::startDetached(QStringLiteral("pkill"), {QStringLiteral("-f"), targetProc});
}

bool ApplicationManager::isAppRunning(const QString &query) const
{
    const QString clean = normalizeQuery(query);
    if (clean.isEmpty()) return false;

    const auto windows = WindowManager::instance().listWindows();
    for (const auto &w : windows) {
        if (w.caption.toLower().contains(clean) || w.resourceClass.toLower().contains(clean)) {
            return true;
        }
    }

    return false;
}

} // namespace Kity
