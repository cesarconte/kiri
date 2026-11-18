/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "itool.h"

namespace Kity {

class CloseAppTool : public ITool
{
public:
    CloseAppTool() = default;
    ~CloseAppTool() override = default;

    QString name() const override { return QStringLiteral("close_application"); }
    QString description() const override {
        return QStringLiteral("Cierra una aplicación o ventana abierta en el escritorio KDE Plasma (ej. Google Maps, Kate, Spotify, Dolphin, pestañas o ventanas de navegador).");
    }
    bool matchesQuery(const QString &query) const override;
    QJsonObject parameters() const override;
    QJsonObject execute(const QJsonObject &arguments) override;
};

} // namespace Kity
