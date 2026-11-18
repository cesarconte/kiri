/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "itool.h"

namespace Kity {

class WeatherTool : public ITool
{
public:
    WeatherTool() = default;
    ~WeatherTool() override = default;

    QString name() const override { return QStringLiteral("get_weather"); }
    QString description() const override {
        return QStringLiteral("Obtiene las condiciones meteorológicas y previsión del tiempo actual para una ciudad o localidad.");
    }
    QJsonObject parameters() const override;
    QJsonObject execute(const QJsonObject &arguments) override;
};

} // namespace Kity
