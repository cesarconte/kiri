/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "itool.h"

namespace Kity {

class TerminalCommandTool : public ITool
{
public:
    TerminalCommandTool() = default;
    ~TerminalCommandTool() override = default;

    QString name() const override { return QStringLiteral("run_terminal_command"); }
    QString description() const override {
        return QStringLiteral("Abre y ejecuta un comando en la terminal Konsole de KDE Plasma en CachyOS Linux (por ejemplo para actualizar el sistema con 'cachy-update', instalar o buscar paquetes con pacman/paru, o monitorización del sistema).");
    }
    bool matchesQuery(const QString &query) const override;
    QJsonObject parameters() const override;
    QJsonObject execute(const QJsonObject &arguments) override;
};

} // namespace Kity
