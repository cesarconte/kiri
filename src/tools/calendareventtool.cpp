/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "calendareventtool.h"
#include "../system/calendarmanager.h"

#include <QJsonArray>
#include <KLocalizedString>

namespace Kity {

QJsonObject CalendarEventTool::parameters() const
{
    QJsonObject props;

    QJsonObject actProp;
    actProp[QStringLiteral("type")] = QStringLiteral("string");
    QJsonArray actEnum;
    actEnum.append(QStringLiteral("create"));
    actEnum.append(QStringLiteral("list"));
    actProp[QStringLiteral("enum")] = actEnum;
    actProp[QStringLiteral("description")] = QStringLiteral("Acción a realizar: 'create' para registrar un nuevo evento o 'list' para consultar eventos próximos.");
    props[QStringLiteral("action")] = actProp;

    QJsonObject titleProp;
    titleProp[QStringLiteral("type")] = QStringLiteral("string");
    titleProp[QStringLiteral("description")] = QStringLiteral("Título o nombre del evento o cita.");
    props[QStringLiteral("title")] = titleProp;

    QJsonObject timeProp;
    timeProp[QStringLiteral("type")] = QStringLiteral("string");
    timeProp[QStringLiteral("description")] = QStringLiteral("Fecha y hora de inicio (ej. '2026-11-18 17:00' o ISO 8601).");
    props[QStringLiteral("start_time")] = timeProp;

    QJsonObject durProp;
    durProp[QStringLiteral("type")] = QStringLiteral("integer");
    durProp[QStringLiteral("description")] = QStringLiteral("Duración estimada del evento en minutos (por defecto 60).");
    props[QStringLiteral("duration_minutes")] = durProp;

    QJsonObject descProp;
    descProp[QStringLiteral("type")] = QStringLiteral("string");
    descProp[QStringLiteral("description")] = QStringLiteral("Descripción o notas adicionales del evento.");
    props[QStringLiteral("description")] = descProp;

    QJsonObject params;
    params[QStringLiteral("type")] = QStringLiteral("object");
    params[QStringLiteral("properties")] = props;
    QJsonArray req;
    req.append(QStringLiteral("action"));
    params[QStringLiteral("required")] = req;

    return params;
}

QJsonObject CalendarEventTool::execute(const QJsonObject &arguments)
{
    const QString action = arguments.value(QStringLiteral("action")).toString().trimmed().toLower();
    QJsonObject result;

    if (action == QLatin1String("create")) {
        const QString title = arguments.value(QStringLiteral("title")).toString().trimmed();
        const QString timeArg = arguments.value(QStringLiteral("start_time")).toString().trimmed();
        const int durationMinutes = arguments.value(QStringLiteral("duration_minutes")).toInt(60);
        const QString description = arguments.value(QStringLiteral("description")).toString().trimmed();

        CalendarEvent ev = CalendarManager::instance().parsePrompt(title + QStringLiteral(" ") + timeArg);
        if (!timeArg.isEmpty() && ev.startTime == QDateTime::currentDateTime().addSecs(3600)) {
            QDateTime explicitDt = QDateTime::fromString(timeArg, Qt::ISODate);
            if (!explicitDt.isValid()) {
                explicitDt = QDateTime::fromString(timeArg, QStringLiteral("yyyy-MM-dd HH:mm"));
            }
            if (explicitDt.isValid()) {
                ev.startTime = explicitDt;
                ev.endTime = explicitDt.addSecs(durationMinutes * 60);
            }
        }
        if (!title.isEmpty()) ev.title = title;
        if (!description.isEmpty()) ev.description = description;

        QString icsPath;
        const bool ok = CalendarManager::instance().createEvent(ev, &icsPath);

        result[QStringLiteral("success")] = ok;
        result[QStringLiteral("action")] = QStringLiteral("create");
        result[QStringLiteral("title")] = ev.title;
        result[QStringLiteral("start_time")] = ev.startTime.toString(Qt::ISODate);
        result[QStringLiteral("calendar_name")] = ev.calendarName;
        result[QStringLiteral("ics_path")] = icsPath;
        result[QStringLiteral("status")] = ok
            ? QStringLiteral("Evento «%1» creado y guardado en KOrganizer (calendario '%2') para el %3.")
                .arg(ev.title, ev.calendarName, ev.startTime.toString(QStringLiteral("dddd d 'de' MMMM 'a las' HH:mm")))
            : QStringLiteral("Error al generar el evento para KOrganizer.");
        return result;
    }

    result[QStringLiteral("error")] = QStringLiteral("Acción no soportada o listado no disponible.");
    return result;
}

} // namespace Kity
