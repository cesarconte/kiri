/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "itool.h"

namespace Kity {

class TimerTool : public ITool
{
public:
    TimerTool() = default;
    ~TimerTool() override = default;

    QString name() const override { return QStringLiteral("set_timer"); }
    QString description() const override {
        return QStringLiteral("Programa un temporizador o alarma en el sistema para que avise al usuario tras un número de segundos.");
    }
    QJsonObject parameters() const override;
    QJsonObject execute(const QJsonObject &arguments) override;
};

} // namespace Kity
