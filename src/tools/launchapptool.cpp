/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "launchapptool.h"
#include "../system/applicationmanager.h"
#include <QJsonArray>
#include <QRegularExpression>

namespace Kity {

bool LaunchAppTool::matchesQuery(const QString &query) const
{
    static const QRegularExpression appSignal(
        QStringLiteral("(abre|abrir|ejecuta|ejecutar|inicia|iniciar|lanza|lanzar|arranca|arrancar)\\s+"),
        QRegularExpression::CaseInsensitiveOption);
    return query.contains(appSignal);
}

QJsonObject LaunchAppTool::parameters() const
{
    QJsonObject props;
    QJsonObject appProp;
    appProp[QStringLiteral("type")] = QStringLiteral("string");
    appProp[QStringLiteral("description")] = QStringLiteral("Nombre común o ejecutable de la aplicación a abrir o enfocar (ej. 'google maps', 'firefox', 'spotify', 'dolphin', 'konsole', 'kate').");
    props[QStringLiteral("app_name")] = appProp;

    QJsonObject params;
    params[QStringLiteral("type")] = QStringLiteral("object");
    params[QStringLiteral("properties")] = props;
    QJsonArray req;
    req.append(QStringLiteral("app_name"));
    params[QStringLiteral("required")] = req;

    return params;
}

QJsonObject LaunchAppTool::execute(const QJsonObject &arguments)
{
    const QString appQuery = arguments.value(QStringLiteral("app_name")).toString().trimmed();
    QString launchedName;
    const bool ok = ApplicationManager::instance().launchApp(appQuery, &launchedName);

    QJsonObject result;
    result[QStringLiteral("success")] = ok;
    result[QStringLiteral("requested_app")] = appQuery;
    result[QStringLiteral("launched_app")] = launchedName.isEmpty() ? appQuery : launchedName;
    return result;
}

} // namespace Kity
