/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace Kity {

struct MediaInfo {
    QString player;
    QString status;
    QString artist;
    QString title;
    QString album;
    QString artUrl;

    bool isValid() const { return !player.isEmpty() && (!title.isEmpty() || !artist.isEmpty()); }
    QVariantMap toMap() const;
};

class MediaManager : public QObject
{
    Q_OBJECT

public:
    static MediaManager &instance();

    MediaInfo queryActiveMedia() const;
    bool executeAction(const QString &action);

    bool play();
    bool pause();
    bool playPause();
    bool next();
    bool previous();
    bool stop();

    // Transient Media Focus (Android MediaSession / AudioFocus style):
    // Pauses currently playing media during voice interaction without touching master sink volume.
    void pauseActiveMedia();
    void resumePausedMedia();
    bool isMediaPaused() const { return m_isMediaPaused; }

private:
    explicit MediaManager(QObject *parent = nullptr);
    ~MediaManager() override = default;

    MediaManager(const MediaManager &) = delete;
    MediaManager &operator=(const MediaManager &) = delete;

    bool m_isMediaPaused = false;
    QStringList m_pausedPlayers;
};

} // namespace Kity
