/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "terminalcommandtool.h"
#include "../system/terminalmanager.h"

#include <QJsonArray>
#include <KLocalizedString>

namespace Kity {

QJsonObject TerminalCommandTool::parameters() const
{
    QJsonObject props;

    QJsonObject cmdProp;
    cmdProp[QStringLiteral("type")] = QStringLiteral("string");
    cmdProp[QStringLiteral("description")] = QStringLiteral("Comando de terminal para CachyOS Linux a ejecutar en Konsole (ej. 'cachy-update', 'paru -Syu', 'fastfetch', 'btop'). NUNCA uses apt ni ubuntu.");
    props[QStringLiteral("command")] = cmdProp;

    QJsonObject titleProp;
    titleProp[QStringLiteral("type")] = QStringLiteral("string");
    titleProp[QStringLiteral("description")] = QStringLiteral("Título opcional de la pestaña de Konsole.");
    props[QStringLiteral("title")] = titleProp;

    QJsonObject autoCloseProp;
    autoCloseProp[QStringLiteral("type")] = QStringLiteral("boolean");
    autoCloseProp[QStringLiteral("description")] = QStringLiteral("Si es verdadero, la terminal se cerrará automáticamente al finalizar si el comando tuvo éxito (código 0).");
    props[QStringLiteral("auto_close")] = autoCloseProp;

    QJsonObject params;
    params[QStringLiteral("type")] = QStringLiteral("object");
    params[QStringLiteral("properties")] = props;
    QJsonArray req;
    req.append(QStringLiteral("command"));
    params[QStringLiteral("required")] = req;

    return params;
}

QJsonObject TerminalCommandTool::execute(const QJsonObject &arguments)
{
    QString cmd = arguments.value(QStringLiteral("command")).toString().trimmed();
    if (cmd.isEmpty()) {
        cmd = arguments.value(QStringLiteral("cmd")).toString().trimmed();
    }
    const QString title = arguments.value(QStringLiteral("title")).toString().trimmed();
    const bool autoClose = arguments.value(QStringLiteral("auto_close")).toBool(false);

    cmd = TerminalManager::instance().sanitizeCommand(cmd);
    const bool started = TerminalManager::instance().executeInKonsole(cmd, title, autoClose);

    QJsonObject result;
    result[QStringLiteral("success")] = started;
    result[QStringLiteral("command")] = cmd;
    result[QStringLiteral("status")] = started
        ? i18n("Terminal Konsole abierta ejecutando '%1'.", cmd)
        : i18n("No se pudo iniciar Konsole.");
    return result;
}

} // namespace Kity
