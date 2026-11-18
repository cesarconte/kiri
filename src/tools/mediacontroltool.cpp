/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "mediacontroltool.h"
#include "../system/mediamanager.h"
#include <QJsonArray>
#include <QRegularExpression>

namespace Kity {

bool MediaControlTool::matchesQuery(const QString &query) const
{
    static const QRegularExpression mediaSignal(
        QStringLiteral("(m[uú]sica|canci[oó]n|cancion|tema|pista|reproduce|reproducir|reproducci[oó]n|reproduciendo|pausa|pausar|det[eé]n|detener|para|parar|stop|mute|silencia|silenciar|volumen|sonido|audio|player|spotify|mpv|vlc|elisa|cantante|album|[aá]lbum|video|v[ií]deo)"),
        QRegularExpression::CaseInsensitiveOption);
    return query.contains(mediaSignal);
}

QJsonObject MediaControlTool::parameters() const
{
    QJsonObject props;
    QJsonObject actProp;
    actProp[QStringLiteral("type")] = QStringLiteral("string");
    QJsonArray actEnum;
    actEnum.append(QStringLiteral("play"));
    actEnum.append(QStringLiteral("pause"));
    actEnum.append(QStringLiteral("stop"));
    actEnum.append(QStringLiteral("next"));
    actEnum.append(QStringLiteral("previous"));
    actEnum.append(QStringLiteral("status"));
    actProp[QStringLiteral("enum")] = actEnum;
    actProp[QStringLiteral("description")] = QStringLiteral("Acción multimedia a ejecutar: 'play' (reproducir o reanudar), 'pause' (pausar), 'stop' (detener), 'next' (siguiente canción), 'previous' (canción anterior), o 'status' (consultar qué está sonando).");
    props[QStringLiteral("action")] = actProp;

    QJsonObject params;
    params[QStringLiteral("type")] = QStringLiteral("object");
    params[QStringLiteral("properties")] = props;
    QJsonArray req;
    req.append(QStringLiteral("action"));
    params[QStringLiteral("required")] = req;

    return params;
}

QJsonObject MediaControlTool::execute(const QJsonObject &arguments)
{
    QJsonObject result;
    const QString action = arguments.value(QStringLiteral("action")).toString().trimmed().toLower();
    result[QStringLiteral("action")] = action;

    if (action == QLatin1String("play") || action == QLatin1String("resume")) {
        MediaManager::instance().play();
        result[QStringLiteral("status")] = QStringLiteral("Reproducción reanudada.");
    } else if (action == QLatin1String("pause") || action == QLatin1String("stop")) {
        MediaManager::instance().pause();
        result[QStringLiteral("status")] = QStringLiteral("Reproducción detenida.");
    } else if (action == QLatin1String("play_pause") || action == QLatin1String("toggle")) {
        MediaManager::instance().playPause();
        result[QStringLiteral("status")] = QStringLiteral("Alternado estado de reproducción.");
    } else if (action == QLatin1String("next")) {
        MediaManager::instance().next();
        result[QStringLiteral("status")] = QStringLiteral("Siguiente pista reproducida.");
    } else if (action == QLatin1String("previous") || action == QLatin1String("prev")) {
        MediaManager::instance().previous();
        result[QStringLiteral("status")] = QStringLiteral("Pista anterior reproducida.");
    } else {
        const auto media = MediaManager::instance().queryActiveMedia();
        if (media.isValid()) {
            result[QStringLiteral("status")] = media.status;
            result[QStringLiteral("track")] = QStringLiteral("%1 - %2").arg(media.artist, media.title);
            result[QStringLiteral("player")] = media.player;
        } else {
            result[QStringLiteral("status")] = QStringLiteral("Sin reproductor activo");
            result[QStringLiteral("track")] = QStringLiteral("");
        }
    }

    return result;
}

} // namespace Kity
