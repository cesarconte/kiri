/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "itool.h"

namespace Kity {

class MediaControlTool : public ITool
{
public:
    MediaControlTool() = default;
    ~MediaControlTool() override = default;

    QString name() const override { return QStringLiteral("media_control"); }
    QString description() const override {
        return QStringLiteral("Controla la reproducción multimedia activa en el sistema (reproducir, pausar, detener, siguiente pista, pista anterior o consultar qué suena en reproductores como mpv, Spotify, VLC, Elisa, navegadores, etc.).");
    }
    QJsonObject parameters() const override;
    QJsonObject execute(const QJsonObject &arguments) override;
};

} // namespace Kity
