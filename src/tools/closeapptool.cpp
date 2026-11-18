/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "closeapptool.h"
#include "../system/applicationmanager.h"
#include <QJsonArray>

namespace Kity {

QJsonObject CloseAppTool::parameters() const
{
    QJsonObject props;
    QJsonObject appProp;
    appProp[QStringLiteral("type")] = QStringLiteral("string");
    appProp[QStringLiteral("description")] = QStringLiteral("Nombre de la aplicación o título de la ventana a cerrar (ej. 'google maps', 'firefox', 'kate', 'spotify', 'dolphin').");
    props[QStringLiteral("app_name")] = appProp;

    QJsonObject params;
    params[QStringLiteral("type")] = QStringLiteral("object");
    params[QStringLiteral("properties")] = props;
    QJsonArray req;
    req.append(QStringLiteral("app_name"));
    params[QStringLiteral("required")] = req;

    return params;
}

QJsonObject CloseAppTool::execute(const QJsonObject &arguments)
{
    const QString appQuery = arguments.value(QStringLiteral("app_name")).toString().trimmed();
    QString closedName;
    const bool ok = ApplicationManager::instance().closeApp(appQuery, &closedName);

    QJsonObject result;
    result[QStringLiteral("success")] = ok;
    result[QStringLiteral("requested_app")] = appQuery;
    result[QStringLiteral("closed_app")] = closedName.isEmpty() ? appQuery : closedName;
    return result;
}

} // namespace Kity
