/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "itool.h"

namespace Kity {

class WebSearchTool : public ITool
{
public:
    WebSearchTool() = default;
    ~WebSearchTool() override = default;

    QString name() const override { return QStringLiteral("web_search"); }
    QString description() const override {
        return QStringLiteral("Realiza una búsqueda en la web en tiempo real para obtener información actualizada sobre noticias, eventos, personas, fechas, hechos o datos recientes.");
    }
    bool matchesQuery(const QString &query) const override;
    QJsonObject parameters() const override;
    QJsonObject execute(const QJsonObject &arguments) override;
};

} // namespace Kity
