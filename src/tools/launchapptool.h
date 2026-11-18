/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "itool.h"

namespace Kity {

class LaunchAppTool : public ITool
{
public:
    LaunchAppTool() = default;
    ~LaunchAppTool() override = default;

    QString name() const override { return QStringLiteral("launch_application"); }
    QString description() const override {
        return QStringLiteral("Abre o ejecuta una aplicación instalada en el sistema KDE Plasma, o trae su ventana al frente si ya está abierta.");
    }
    QJsonObject parameters() const override;
    QJsonObject execute(const QJsonObject &arguments) override;
};

} // namespace Kity
