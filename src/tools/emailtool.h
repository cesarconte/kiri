/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "itool.h"

namespace Kity {

class EmailTool : public ITool
{
public:
    EmailTool() = default;
    ~EmailTool() override = default;

    QString name() const override { return QStringLiteral("email_tool"); }
    QString description() const override {
        return QStringLiteral("Prepara, envía o descarta borradores de correo electrónico mediante Proton Mail (y Proton Mail Bridge) con soporte de estilos y confirmación de seguridad.");
    }

    bool matchesQuery(const QString &query) const override;
    QJsonObject parameters() const override;
    QJsonObject execute(const QJsonObject &arguments) override;
};

} // namespace Kity
