/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "itool.h"

namespace Kity {

class SystemVitalsTool : public ITool
{
public:
    SystemVitalsTool() = default;
    ~SystemVitalsTool() override = default;

    QString name() const override { return QStringLiteral("get_system_vitals"); }
    QString description() const override {
        return QStringLiteral("Consulta el estado en tiempo real de los recursos del equipo: memoria RAM libre y total, uso de CPU, almacenamiento en disco, batería y perfil de energía.");
    }
    bool matchesQuery(const QString &query) const override;
    QJsonObject parameters() const override;
    QJsonObject execute(const QJsonObject &arguments) override;
};

} // namespace Kity
