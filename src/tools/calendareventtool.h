/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "itool.h"

namespace Kity {

class CalendarEventTool : public ITool
{
public:
    CalendarEventTool() = default;
    ~CalendarEventTool() override = default;

    QString name() const override { return QStringLiteral("calendar_event"); }
    QString description() const override {
        return QStringLiteral("Gestiona la agenda y el calendario de KDE (KOrganizer) con sincronización de Google Calendar (calendario 'César Conte'): permite crear eventos, citas y reuniones o consultar la lista de eventos programados.");
    }
    bool matchesQuery(const QString &query) const override;
    QJsonObject parameters() const override;
    QJsonObject execute(const QJsonObject &arguments) override;
};

} // namespace Kity
