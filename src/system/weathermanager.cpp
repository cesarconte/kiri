/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "weathermanager.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

namespace Kity {

QVariantMap WeatherData::toMap() const
{
    QVariantMap data;
    data[QStringLiteral("location")] = location;
    data[QStringLiteral("city")] = city;
    data[QStringLiteral("temp")] = temp;
    data[QStringLiteral("feelsLike")] = feelsLike;
    data[QStringLiteral("condition")] = condition;
    data[QStringLiteral("humidity")] = humidity;
    data[QStringLiteral("wind")] = wind;
    data[QStringLiteral("precipitation")] = precipitation;
    data[QStringLiteral("wmoCode")] = wmoCode;
    data[QStringLiteral("iconName")] = iconName;
    return data;
}

WeatherManager &WeatherManager::instance()
{
    static WeatherManager s_instance;
    return s_instance;
}

WeatherManager::WeatherManager(QObject *parent)
    : QObject(parent)
{
}

QByteArray WeatherManager::httpGetSync(const QUrl &url, int timeoutMs) const
{
    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Kity-Plasma-Assistant/1.0"));

    QNetworkReply *reply = nam.get(req);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    timer.start(timeoutMs);
    loop.exec();

    QByteArray data;
    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        data = reply->readAll();
    }
    reply->deleteLater();
    return data;
}

QString WeatherManager::wmoCodeToSpanish(int code)
{
    switch (code) {
    case 0: return QStringLiteral("despejado");
    case 1: return QStringLiteral("mayormente despejado");
    case 2: return QStringLiteral("parcialmente nuboso");
    case 3: return QStringLiteral("nublado");
    case 45: case 48: return QStringLiteral("niebla");
    case 51: case 53: case 55: return QStringLiteral("llovizna");
    case 61: case 63: case 65: return QStringLiteral("lluvia");
    case 66: case 67: return QStringLiteral("lluvia helada");
    case 71: case 73: case 75: return QStringLiteral("nieve");
    case 77: return QStringLiteral("granizo fino");
    case 80: case 81: case 82: return QStringLiteral("chubascos");
    case 85: case 86: return QStringLiteral("chubascos de nieve");
    case 95: return QStringLiteral("tormenta");
    case 96: case 99: return QStringLiteral("tormenta con granizo");
    default: return QStringLiteral("variable");
    }
}

QString WeatherManager::wmoCodeToIcon(int code)
{
    if (code == 0) return QStringLiteral("weather-clear");
    if (code == 1) return QStringLiteral("weather-few-clouds");
    if (code == 2) return QStringLiteral("weather-clouds");
    if (code == 3) return QStringLiteral("weather-overcast");
    if (code == 45 || code == 48) return QStringLiteral("weather-fog");
    if (code >= 51 && code <= 55) return QStringLiteral("weather-showers-scattered");
    if (code >= 61 && code <= 65) return QStringLiteral("weather-showers");
    if (code >= 66 && code <= 67) return QStringLiteral("weather-freezing-rain");
    if (code >= 71 && code <= 77) return QStringLiteral("weather-snow");
    if (code >= 80 && code <= 82) return QStringLiteral("weather-storm");
    if (code >= 85 && code <= 86) return QStringLiteral("weather-snow-rain");
    if (code >= 95) return QStringLiteral("weather-severe-alert");
    return QStringLiteral("weather-few-clouds");
}

QString WeatherManager::extractCityFromQuery(const QString &query) const
{
    static const QRegularExpression cityRegex(
        QStringLiteral("(?:en|de|para|sobre)\\s+([A-ZÁÉÍÓÚa-záéíóúñÑçÇ\\s\\-\\.]+?)(?:\\s+(?:hoy|ahora(?:\\s+mismo)?|mañana|esta\\s+tarde|esta\\s+noche|el\\s+fin\\s+de\\s+semana|por\\s+favor)|[\\?\\!\\.,;]*$)"),
        QRegularExpression::CaseInsensitiveOption);

    const auto match = cityRegex.match(query);
    if (match.hasMatch()) {
        QString city = match.captured(1).trimmed();
        city.remove(QRegularExpression(QStringLiteral("[\\?\\!\\.,;]+$")));
        if (!city.isEmpty()) {
            return city.trimmed();
        }
    }
    return QString();
}

QString WeatherManager::resolveDefaultLocation() const
{
    // Try detecting city from Plasma weather widgets in desktop-appletsrc
    QFile f(QDir::homePath() + QStringLiteral("/.config/plasma-org.kde.plasma.desktop-appletsrc"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (line.startsWith(QStringLiteral("locationName="))) {
                const QString loc = line.mid(13).trimmed();
                if (!loc.isEmpty()) {
                    const QString first = loc.section(QLatin1Char(','), 0, 0).trimmed();
                    if (!first.isEmpty()) {
                        if (first.contains(QStringLiteral("Calzada"), Qt::CaseInsensitive)) {
                            return QStringLiteral("Gijón");
                        }
                        return first;
                    }
                }
            }
        }
    }
    return QStringLiteral("Gijón");
}

WeatherData WeatherManager::fetchWeatherData(const QString &query) const
{
    WeatherData wd;

    QString targetCity = extractCityFromQuery(query);
    if (targetCity.isEmpty()) {
        targetCity = query.trimmed();
    }
    targetCity.remove(QRegularExpression(QStringLiteral("^(?:tiempo|clima|temperatura)\\s+(?:en|de|para)?\\s*"), QRegularExpression::CaseInsensitiveOption));
    targetCity = targetCity.trimmed();

    if (targetCity.isEmpty()) {
        targetCity = resolveDefaultLocation();
    }

    const QUrl geoUrl(QStringLiteral("https://geocoding-api.open-meteo.com/v1/search?name=%1&count=1&language=es&format=json")
                          .arg(QUrl::toPercentEncoding(targetCity)));
    const QByteArray geoResp = httpGetSync(geoUrl, 2500);
    if (geoResp.isEmpty()) {
        return wd;
    }

    const QJsonDocument geoDoc = QJsonDocument::fromJson(geoResp);
    const QJsonArray results = geoDoc.object().value(QStringLiteral("results")).toArray();
    if (results.isEmpty()) {
        return wd;
    }

    const QJsonObject locObj = results.first().toObject();
    const double lat = locObj.value(QStringLiteral("latitude")).toDouble();
    const double lon = locObj.value(QStringLiteral("longitude")).toDouble();
    const QString resolvedName = locObj.value(QStringLiteral("name")).toString();
    const QString admin = locObj.value(QStringLiteral("admin1")).toString();
    const QString country = locObj.value(QStringLiteral("country")).toString();

    const QUrl weatherUrl(QStringLiteral("https://api.open-meteo.com/v1/forecast?latitude=%1&longitude=%2&current=temperature_2m,relative_humidity_2m,apparent_temperature,precipitation,weather_code,wind_speed_10m&timezone=auto")
                              .arg(lat, 0, 'f', 4)
                              .arg(lon, 0, 'f', 4));
    const QByteArray weatherResp = httpGetSync(weatherUrl, 2500);
    if (weatherResp.isEmpty()) {
        return wd;
    }

    const QJsonDocument wDoc = QJsonDocument::fromJson(weatherResp);
    const QJsonObject current = wDoc.object().value(QStringLiteral("current")).toObject();
    if (current.isEmpty()) {
        return wd;
    }

    wd.temp = current.value(QStringLiteral("temperature_2m")).toDouble();
    wd.feelsLike = current.value(QStringLiteral("apparent_temperature")).toDouble();
    wd.wmoCode = current.value(QStringLiteral("weather_code")).toInt();
    wd.wind = current.value(QStringLiteral("wind_speed_10m")).toDouble();
    wd.humidity = current.value(QStringLiteral("relative_humidity_2m")).toDouble();
    wd.precipitation = current.value(QStringLiteral("precipitation")).toDouble();

    wd.condition = wmoCodeToSpanish(wd.wmoCode);
    wd.iconName = wmoCodeToIcon(wd.wmoCode);

    QString fullLoc = resolvedName;
    if (!admin.isEmpty() && admin != resolvedName) {
        fullLoc.append(QStringLiteral(", %1").arg(admin));
    }
    if (!country.isEmpty()) {
        fullLoc.append(QStringLiteral(" (%1)").arg(country));
    }
    wd.location = fullLoc;
    wd.city = resolvedName;
    wd.isValid = true;

    return wd;
}

QString WeatherManager::formatWeatherPromptContext(const WeatherData &data) const
{
    if (!data.isValid) {
        return QString();
    }

    QString conditionPhrase;
    if (data.condition.contains(QLatin1String("despejado")) || data.condition.contains(QLatin1String("nub"))) {
        conditionPhrase = QStringLiteral("cielo %1").arg(data.condition);
    } else {
        conditionPhrase = data.condition;
    }

    return QStringLiteral(
        "[Datos meteorológicos oficiales y actualizados en tiempo real para %1:\n"
        "- Temperatura actual: %2 °C\n"
        "- Sensación térmica: %3 °C\n"
        "- Estado del tiempo: %4\n"
        "- Humedad relativa: %5 %\n"
        "- Viento: %6 km/h\n"
        "- Precipitaciones: %7 mm\n"
        "INSTRUCCIÓN OBLIGATORIA: Eres Kity, el asistente de voz. Responde a la pregunta del usuario indicando claramente que en %8 hace %2 °C con %9 (o usando una expresión natural en español como 'en %8 hace %2 °C y está lloviendo' o 'hace %2 °C con %9'). Tu respuesta debe ser gramaticalmente correcta en castellano, concisa, directa y natural en una sola frase optimizada para hablar por voz al estilo Siri. No digas que no tienes acceso a información en tiempo real.]")
        .arg(data.location)
        .arg(QString::number(data.temp, 'f', 1))
        .arg(QString::number(data.feelsLike, 'f', 1))
        .arg(data.condition)
        .arg(QString::number(data.humidity, 'f', 0))
        .arg(QString::number(data.wind, 'f', 1))
        .arg(QString::number(data.precipitation, 'f', 1))
        .arg(data.city)
        .arg(conditionPhrase);
}

} // namespace Kity
