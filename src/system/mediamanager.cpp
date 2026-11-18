/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "mediamanager.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
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

void MediaManager::pauseActiveMedia()
{
    if (m_isMediaPaused) {
        return;
    }
    m_isMediaPaused = true;
    m_pausedPlayers.clear();

    // Discover all active MPRIS media players and pause those currently Playing
    // (exact behavior of Android MediaSession / AudioFocus on incoming calls/assistant)
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        QDBusConnectionInterface *iface = bus.interface();
        if (iface) {
            const QStringList services = iface->registeredServiceNames().value();
            for (const QString &svc : services) {
                if (svc.startsWith(QLatin1String("org.mpris.MediaPlayer2.")) && !svc.contains(QLatin1String("kdeconnect"))) {
                    QDBusInterface playerIface(svc,
                                               QStringLiteral("/org/mpris/MediaPlayer2"),
                                               QStringLiteral("org.mpris.MediaPlayer2.Player"),
                                               bus);
                    if (playerIface.isValid()) {
                        const QString status = playerIface.property("PlaybackStatus").toString();
                        if (status == QLatin1String("Playing")) {
                            m_pausedPlayers.append(svc);
                            playerIface.call(QDBus::NoBlock, QStringLiteral("Pause"));
                        }
                    }
                }
            }
        }
    }

    // Fallback: If no specific DBus player was detected, check playerctl
    if (m_pausedPlayers.isEmpty()) {
        QProcess p;
        p.start(QStringLiteral("playerctl"), {QStringLiteral("status")});
        if (p.waitForFinished(150) && p.exitCode() == 0) {
            const QString st = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
            if (st.contains(QLatin1String("Playing"), Qt::CaseInsensitive)) {
                m_pausedPlayers.append(QStringLiteral("__playerctl_all__"));
                QProcess::execute(QStringLiteral("playerctl"), {QStringLiteral("pause")});
            }
        }
    }
}

void MediaManager::resumePausedMedia()
{
    if (!m_isMediaPaused) {
        return;
    }
    m_isMediaPaused = false;

    // Resume only the players that were actually Playing before Kity paused them
    QDBusConnection bus = QDBusConnection::sessionBus();
    for (const QString &svc : m_pausedPlayers) {
        if (svc == QLatin1String("__playerctl_all__")) {
            QProcess::execute(QStringLiteral("playerctl"), {QStringLiteral("play")});
            continue;
        }
        if (bus.isConnected()) {
            QDBusInterface playerIface(svc,
                                       QStringLiteral("/org/mpris/MediaPlayer2"),
                                       QStringLiteral("org.mpris.MediaPlayer2.Player"),
                                       bus);
            if (playerIface.isValid()) {
                playerIface.call(QDBus::NoBlock, QStringLiteral("Play"));
            }
        }
    }
    m_pausedPlayers.clear();
}

} // namespace Kity
