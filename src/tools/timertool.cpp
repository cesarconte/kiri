/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "timertool.h"
#include "../system/timermanager.h"
#include <QJsonArray>
#include <QRegularExpression>

namespace Kity {

bool TimerTool::matchesQuery(const QString &query) const
{
    static const QRegularExpression timerSignal(
        QStringLiteral("(temporizador|alarma|av[ií]same|cuenta\\s+atr[aá]s|recu[eé]rdame|minuto|segundo|recordatorio)"),
        QRegularExpression::CaseInsensitiveOption);
    return query.contains(timerSignal);
}

QJsonObject TimerTool::parameters() const
{
    QJsonObject props;
    QJsonObject secProp;
    secProp[QStringLiteral("type")] = QStringLiteral("integer");
    secProp[QStringLiteral("description")] = QStringLiteral("Duración del temporizador en segundos totales (por ejemplo 300 para 5 minutos).");
    props[QStringLiteral("seconds")] = secProp;

    QJsonObject lblProp;
    lblProp[QStringLiteral("type")] = QStringLiteral("string");
    lblProp[QStringLiteral("description")] = QStringLiteral("Motivo o etiqueta del temporizador (ej. 'pizza', 'pasta', 'reunión').");
    props[QStringLiteral("label")] = lblProp;

    QJsonObject params;
    params[QStringLiteral("type")] = QStringLiteral("object");
    params[QStringLiteral("properties")] = props;
    QJsonArray req;
    req.append(QStringLiteral("seconds"));
    params[QStringLiteral("required")] = req;

    return params;
}

QJsonObject TimerTool::execute(const QJsonObject &arguments)
{
    int seconds = arguments.value(QStringLiteral("seconds")).toInt();
    if (seconds <= 0) {
        seconds = 60;
    }
    const QString label = arguments.value(QStringLiteral("label")).toString().trimmed();

    const ScheduledReminder rem = TimerManager::instance().addTimer(seconds, label);

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    result[QStringLiteral("seconds")] = seconds;
    result[QStringLiteral("label")] = rem.text;
    result[QStringLiteral("expires_at")] = rem.targetTime.toString(Qt::ISODate);
    return result;
}

} // namespace Kity
