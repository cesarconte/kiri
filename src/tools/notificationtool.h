/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "itool.h"

namespace Kity {

class NotificationTool : public ITool
{
public:
    NotificationTool() = default;
    ~NotificationTool() override = default;

    QString name() const override { return QStringLiteral("summarize_notifications"); }
    QString description() const override {
        return QStringLiteral("Sintetiza, resume y prioriza avisos y notificaciones recientes del sistema (estilo Apple Intelligence), destacando alertas críticas y recordatorios pendientes.");
    }

    QJsonObject parameters() const override;
    QJsonObject execute(const QJsonObject &arguments) override;
};

} // namespace Kity
