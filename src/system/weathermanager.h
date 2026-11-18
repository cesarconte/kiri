/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace Kity {

struct WeatherData {
    QString location;
    QString city;
    double temp = 0.0;
    double feelsLike = 0.0;
    QString condition;
    double humidity = 0.0;
    double wind = 0.0;
    double precipitation = 0.0;
    int wmoCode = 0;
    QString iconName;
    bool isValid = false;

    bool isForecast = false;
    QString forecastPeriod;
    double tempMax = 0.0;
    double tempMin = 0.0;
    int precipProbability = 0;

    QVariantMap toMap() const;
};

class WeatherManager : public QObject
{
    Q_OBJECT

public:
    static WeatherManager &instance();

    WeatherData fetchWeatherData(const QString &query) const;
    QString formatWeatherPromptContext(const WeatherData &data) const;

    QString resolveDefaultLocation() const;
    QString extractCityFromQuery(const QString &query) const;
    static QString wmoCodeToSpanish(int code);
    static QString wmoCodeToIcon(int code);

private:
    explicit WeatherManager(QObject *parent = nullptr);
    ~WeatherManager() override = default;

    WeatherManager(const WeatherManager &) = delete;
    WeatherManager &operator=(const WeatherManager &) = delete;

    QByteArray httpGetSync(const QUrl &url, int timeoutMs = 2500) const;
};

} // namespace Kity
