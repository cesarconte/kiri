/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "weathertool.h"
#include "../system/weathermanager.h"
#include <KLocalizedString>

namespace Kity {

QJsonObject WeatherTool::parameters() const
{
    QJsonObject props;
    QJsonObject cityProp;
    cityProp[QStringLiteral("type")] = QStringLiteral("string");
    cityProp[QStringLiteral("description")] = QStringLiteral("Nombre de la ciudad o localidad (ej. Gijón, Oviedo, Madrid, París). Si el usuario no menciona ciudad, omitir.");
    props[QStringLiteral("city")] = cityProp;

    QJsonObject params;
    params[QStringLiteral("type")] = QStringLiteral("object");
    params[QStringLiteral("properties")] = props;

    return params;
}

QJsonObject WeatherTool::execute(const QJsonObject &arguments)
{
    QString city = arguments.value(QStringLiteral("city")).toString().trimmed();
    if (city.isEmpty()) {
        city = WeatherManager::instance().resolveDefaultLocation();
    }

    const WeatherData data = WeatherManager::instance().fetchWeatherData(city);
    const QString report = WeatherManager::instance().formatWeatherPromptContext(data);

    QJsonObject result;
    result[QStringLiteral("city")] = city;
    result[QStringLiteral("weather_report")] = report.isEmpty() ?
        i18n("No se pudieron obtener datos meteorológicos para %1 en este momento.", city) : report;
    return result;
}

} // namespace Kity
