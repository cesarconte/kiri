/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "mediamanager.h"

#include <QProcess>
#include <QStringList>

namespace Kity {

QVariantMap MediaInfo::toMap() const
{
    QVariantMap res;
    res[QStringLiteral("player")] = player;
    res[QStringLiteral("status")] = status;
    res[QStringLiteral("artist")] = artist;
    res[QStringLiteral("title")] = title;
    res[QStringLiteral("album")] = album;
    res[QStringLiteral("artUrl")] = artUrl;
    return res;
}

MediaManager &MediaManager::instance()
{
    static MediaManager s_instance;
    return s_instance;
}

MediaManager::MediaManager(QObject *parent)
    : QObject(parent)
{
}

MediaInfo MediaManager::queryActiveMedia() const
{
    MediaInfo info;
    QProcess p;
    p.start(QStringLiteral("playerctl"), {
        QStringLiteral("metadata"),
        QStringLiteral("--format"),
        QStringLiteral("{{ playerName }}|||{{ status }}|||{{ artist }}|||{{ title }}|||{{ album }}|||{{ mpris:artUrl }}")
    });

    if (!p.waitForFinished(1000) || p.exitCode() != 0) {
        return info;
    }

    const QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    if (out.isEmpty()) {
        return info;
    }

    const QStringList parts = out.split(QStringLiteral("|||"));
    if (parts.size() >= 4) {
        info.player = parts.value(0).trimmed();
        info.status = parts.value(1).trimmed();
        info.artist = parts.value(2).trimmed();
        info.title = parts.value(3).trimmed();
        info.album = parts.value(4).trimmed();
        info.artUrl = parts.value(5).trimmed();
    }

    return info;
}

bool MediaManager::executeAction(const QString &action)
{
    const QString act = action.trimmed().toLower();
    if (act == QStringLiteral("play-pause") || act == QStringLiteral("toggle")) {
        return playPause();
    } else if (act == QStringLiteral("play") || act == QStringLiteral("resume")) {
        return play();
    } else if (act == QStringLiteral("pause")) {
        return pause();
    } else if (act == QStringLiteral("next")) {
        return next();
    } else if (act == QStringLiteral("previous") || act == QStringLiteral("prev")) {
        return previous();
    } else if (act == QStringLiteral("stop")) {
        return stop();
    }
    return false;
}

bool MediaManager::play()
{
    return QProcess::execute(QStringLiteral("playerctl"), {QStringLiteral("play")}) == 0;
}

bool MediaManager::pause()
{
    return QProcess::execute(QStringLiteral("playerctl"), {QStringLiteral("pause")}) == 0;
}

bool MediaManager::playPause()
{
    return QProcess::execute(QStringLiteral("playerctl"), {QStringLiteral("play-pause")}) == 0;
}

bool MediaManager::next()
{
    return QProcess::execute(QStringLiteral("playerctl"), {QStringLiteral("next")}) == 0;
}

bool MediaManager::previous()
{
    return QProcess::execute(QStringLiteral("playerctl"), {QStringLiteral("previous")}) == 0;
}

bool MediaManager::stop()
{
    return QProcess::execute(QStringLiteral("playerctl"), {QStringLiteral("stop")}) == 0;
}

} // namespace Kity
