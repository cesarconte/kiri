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
    data[QStringLiteral("isForecast")] = isForecast;
    data[QStringLiteral("forecastPeriod")] = forecastPeriod;
    data[QStringLiteral("tempMax")] = tempMax;
    data[QStringLiteral("tempMin")] = tempMin;
    data[QStringLiteral("precipProbability")] = precipProbability;
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
    QString q = query;
    // Normalize Whisper phonetics: Jijón -> Gijón
    q.replace(QRegularExpression(QStringLiteral(R"(\b[Jj]ij[oó]n\b)")), QStringLiteral("Gijón"));

    // Remove leading punctuation
    q.remove(QRegularExpression(QStringLiteral(R"(^[¿\?¡\!]+)")));

    // Remove common weather request prefixes so they don't capture "para" or prepositions
    q.remove(QRegularExpression(QStringLiteral(R"(^(?:dime\s+)?(?:cu[aá]l\s+es\s+la\s+predicci[oó]n\s+(?:del\s+tiempo|meteorol[oó]gica)|predicci[oó]n\s+del\s+tiempo|previsi[oó]n\s+(?:del\s+tiempo|meteorol[oó]gica)|qu[eé]\s+tiempo\s+(?:hace|har[aá]|haga|va\s+a\s+hacer|espera)|el\s+tiempo(?:\s+que\s+(?:hace|har[aá]|haga))?|temperatura|clima)\s+)"), QRegularExpression::CaseInsensitiveOption));

    // Remove temporal expressions ("para mañana", "mañana", "para hoy", "hoy", etc.)
    q.remove(QRegularExpression(QStringLiteral(R"(\b(?:para\s+)?(?:hoy|mañana|pasado\s+mañana|ahora(?:\\s+mismo)?|esta\s+tarde|esta\s+noche|el\s+fin\s+de\s+semana)\b)"), QRegularExpression::CaseInsensitiveOption));

    static const QRegularExpression cityRegex(
        QStringLiteral("(?:en|de|para|sobre)\\s+([A-ZÁÉÍÓÚa-záéíóúñÑçÇ\\s\\-\\.]+?)[\\?\\!\\.,;]*$"),
        QRegularExpression::CaseInsensitiveOption);

    const auto match = cityRegex.match(q);
    if (match.hasMatch()) {
        QString city = match.captured(1).trimmed();
        city.remove(QRegularExpression(QStringLiteral("[\\?\\!\\.,;]+$")));
        if (!city.isEmpty()) {
            return city.trimmed();
        }
    }

    // Fallback: check if the remaining text itself is a city name
    q = q.trimmed();
    q.remove(QRegularExpression(QStringLiteral("^[\\?\\!\\.,;\\s]+|[\\?\\!\\.,;\\s]+$")));
    if (!q.isEmpty() && q.length() < 35 && !q.contains(QLatin1Char(' '))) {
        return q;
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

    const bool isTomorrow = query.contains(QRegularExpression(QStringLiteral(R"(\bmañana\b)"), QRegularExpression::CaseInsensitiveOption))
                         && !query.contains(QRegularExpression(QStringLiteral(R"(\bpasado\s+mañana\b)"), QRegularExpression::CaseInsensitiveOption));
    const bool isDayAfterTomorrow = query.contains(QRegularExpression(QStringLiteral(R"(\bpasado\s+mañana\b)"), QRegularExpression::CaseInsensitiveOption));

    QString targetCity = extractCityFromQuery(query);
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

    const QUrl weatherUrl(QStringLiteral("https://api.open-meteo.com/v1/forecast?latitude=%1&longitude=%2&current=temperature_2m,relative_humidity_2m,apparent_temperature,precipitation,weather_code,wind_speed_10m&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,precipitation_sum,wind_speed_10m_max&timezone=auto")
                              .arg(lat, 0, 'f', 4)
                              .arg(lon, 0, 'f', 4));
    const QByteArray weatherResp = httpGetSync(weatherUrl, 2500);
    if (weatherResp.isEmpty()) {
        return wd;
    }

    const QJsonDocument wDoc = QJsonDocument::fromJson(weatherResp);
    const QJsonObject current = wDoc.object().value(QStringLiteral("current")).toObject();
    const QJsonObject daily = wDoc.object().value(QStringLiteral("daily")).toObject();

    if (current.isEmpty() && daily.isEmpty()) {
        return wd;
    }

    if (isTomorrow || isDayAfterTomorrow) {
        const int forecastDay = isDayAfterTomorrow ? 2 : 1;
        const QJsonArray codes = daily.value(QStringLiteral("weather_code")).toArray();
        const QJsonArray maxTemps = daily.value(QStringLiteral("temperature_2m_max")).toArray();
        const QJsonArray minTemps = daily.value(QStringLiteral("temperature_2m_min")).toArray();
        const QJsonArray precips = daily.value(QStringLiteral("precipitation_sum")).toArray();
        const QJsonArray probs = daily.value(QStringLiteral("precipitation_probability_max")).toArray();
        const QJsonArray winds = daily.value(QStringLiteral("wind_speed_10m_max")).toArray();

        if (forecastDay < codes.size()) {
            wd.isForecast = true;
            wd.forecastPeriod = isDayAfterTomorrow ? QStringLiteral("pasado mañana") : QStringLiteral("mañana");
            wd.wmoCode = codes.at(forecastDay).toInt();
            wd.tempMax = maxTemps.at(forecastDay).toDouble();
            wd.tempMin = minTemps.at(forecastDay).toDouble();
            wd.temp = wd.tempMax;
            wd.feelsLike = wd.tempMax;
            wd.precipitation = precips.at(forecastDay).toDouble();
            wd.precipProbability = probs.at(forecastDay).toInt();
            wd.wind = winds.at(forecastDay).toDouble();
            wd.condition = wmoCodeToSpanish(wd.wmoCode);
            wd.iconName = wmoCodeToIcon(wd.wmoCode);
        }
    }

    if (!wd.isForecast && !current.isEmpty()) {
        wd.temp = current.value(QStringLiteral("temperature_2m")).toDouble();
        wd.feelsLike = current.value(QStringLiteral("apparent_temperature")).toDouble();
        wd.wmoCode = current.value(QStringLiteral("weather_code")).toInt();
        wd.wind = current.value(QStringLiteral("wind_speed_10m")).toDouble();
        wd.humidity = current.value(QStringLiteral("relative_humidity_2m")).toDouble();
        wd.precipitation = current.value(QStringLiteral("precipitation")).toDouble();
        wd.condition = wmoCodeToSpanish(wd.wmoCode);
        wd.iconName = wmoCodeToIcon(wd.wmoCode);
    }

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

static QString naturalForecastCondition(int code, const QString &condition)
{
    switch (code) {
    case 0: return QStringLiteral("los cielos estarán despejados");
    case 1: return QStringLiteral("el cielo estará mayormente despejado");
    case 2: return QStringLiteral("habrá intervalos nubosos");
    case 3: return QStringLiteral("estará nublado");
    case 45: case 48: return QStringLiteral("habrá bancos de niebla");
    case 51: case 53: case 55: return QStringLiteral("se esperan lloviznas");
    case 61: case 63: case 65: return QStringLiteral("se esperan lluvias");
    case 66: case 67: return QStringLiteral("se esperan lluvias heladas");
    case 71: case 73: case 75: return QStringLiteral("se esperan nevadas");
    case 80: case 81: case 82: return QStringLiteral("se esperan chubascos");
    case 95: case 96: case 99: return QStringLiteral("se esperan tormentas");
    default:
        if (condition.contains(QLatin1String("despejado"), Qt::CaseInsensitive)) {
            return QStringLiteral("estará despejado");
        }
        if (condition.contains(QLatin1String("lluv"), Qt::CaseInsensitive)) {
            return QStringLiteral("se esperan lluvias");
        }
        if (condition.contains(QLatin1String("nub"), Qt::CaseInsensitive)) {
            return QStringLiteral("estará nublado");
        }
        return QStringLiteral("estará %1").arg(condition);
    }
}

QString WeatherManager::formatWeatherPromptContext(const WeatherData &data) const
{
    if (!data.isValid) {
        return QString();
    }

    if (data.isForecast) {
        const QString periodCap = data.forecastPeriod.isEmpty()
            ? QStringLiteral("Mañana")
            : (data.forecastPeriod.at(0).toUpper() + data.forecastPeriod.mid(1));
        const QString condText = naturalForecastCondition(data.wmoCode, data.condition);
        const QString tempMaxStr = QString::number(qRound(data.tempMax));
        const QString tempMinStr = QString::number(qRound(data.tempMin));

        QString rainExtra;
        QString rainBullet;
        if (data.precipProbability >= 30) {
            rainExtra = QStringLiteral(" y un %1 % de probabilidad de precipitaciones").arg(data.precipProbability);
            rainBullet = QStringLiteral("\n- Probabilidad de precipitación: %1 %").arg(data.precipProbability);
        }

        return QStringLiteral(
            "[Previsión meteorológica oficial para %1 (%2)]:\n"
            "- Estado del cielo: %3\n"
            "- Temperatura mínima: %4 °C\n"
            "- Temperatura máxima: %5 °C%6\n\n"
            "Responde a la pregunta del usuario indicando obligatoriamente el estado del cielo y ambas temperaturas (mínima y máxima). Ejemplo de respuesta requerida:\n"
            "\"%7 en %8 %9, con una temperatura mínima de %4 °C y una máxima de %5 °C%10.\"\n"
            "REGLA ESTRICTA: Menciona siempre tanto la mínima (%4 °C) como la máxima (%5 °C). Responde en una sola frase natural y profesional en español de España.")
            .arg(data.location)          // %1
            .arg(data.forecastPeriod)    // %2
            .arg(data.condition)         // %3
            .arg(tempMinStr)             // %4
            .arg(tempMaxStr)             // %5
            .arg(rainBullet)             // %6
            .arg(periodCap)              // %7
            .arg(data.city)              // %8
            .arg(condText)               // %9
            .arg(rainExtra);             // %10
    }

    QString conditionPhrase;
    if (data.condition.contains(QLatin1String("despejado")) || data.condition.contains(QLatin1String("nub"))) {
        conditionPhrase = QStringLiteral("cielo %1").arg(data.condition);
    } else {
        conditionPhrase = data.condition;
    }

    return QStringLiteral(
        "[Datos meteorológicos oficiales para %1 (actualmente)]:\n"
        "- Estado del tiempo: %4\n"
        "- Temperatura actual: %2 °C\n"
        "- Sensación térmica: %3 °C\n"
        "- Humedad relativa: %5 %\n"
        "- Viento: %6 km/h\n"
        "- Precipitaciones: %7 mm\n\n"
        "Responde a la pregunta del usuario indicando claramente que en %8 hace %2 °C con %9 (o usando una expresión natural en español como 'en %8 hace %2 °C y está lloviendo' o 'hace %2 °C con %9'). Tu respuesta debe ser concisa, directa y natural en una sola frase en español de España.")
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
