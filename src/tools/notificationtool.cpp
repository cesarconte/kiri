/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "notificationtool.h"
#include "../system/notificationmanager.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <KLocalizedString>

namespace Kity {

bool NotificationTool::matchesQuery(const QString &query) const
{
    static const QRegularExpression notifToolSignal(
        QStringLiteral("(notificaci[oó]n|notificaciones|avisos|alertas|mensajes\\s+pendientes)"),
        QRegularExpression::CaseInsensitiveOption);
    return query.contains(notifToolSignal);
}

QJsonObject NotificationTool::parameters() const
{
    QJsonObject props;

    QJsonObject hoursProp;
    hoursProp[QStringLiteral("type")] = QStringLiteral("integer");
    hoursProp[QStringLiteral("description")] = QStringLiteral("Horas hacia atrás a consultar (por defecto 2).");
    props[QStringLiteral("hours")] = hoursProp;

    QJsonObject filterProp;
    filterProp[QStringLiteral("type")] = QStringLiteral("string");
    filterProp[QStringLiteral("description")] = QStringLiteral("Filtro opcional por aplicación o severidad ('critical', 'normal', etc.).");
    props[QStringLiteral("filter")] = filterProp;

    QJsonObject params;
    params[QStringLiteral("type")] = QStringLiteral("object");
    params[QStringLiteral("properties")] = props;

    return params;
}

QJsonObject NotificationTool::execute(const QJsonObject &arguments)
{
    const int hours = arguments.value(QStringLiteral("hours")).toInt(2);
    const QString filter = arguments.value(QStringLiteral("filter")).toString();

    const QString context = NotificationManager::instance().collectRecentNotificationsContext(hours);
    const auto items = NotificationManager::instance().recentNotifications(hours);

    QJsonArray itemsArr;
    for (const auto &item : items) {
        itemsArr.append(item.toJson());
    }

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    result[QStringLiteral("notifications_summary")] = context;
    result[QStringLiteral("items")] = itemsArr;
    result[QStringLiteral("count")] = items.size();
    return result;
}

} // namespace Kity
