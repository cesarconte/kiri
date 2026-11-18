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

    // Auto-Ducking and Media Focus management
    void startAudioDucking();
    void stopAudioDucking();
    bool isDucked() const { return m_isDucked; }

private:
    explicit MediaManager(QObject *parent = nullptr);
    ~MediaManager() override = default;

    MediaManager(const MediaManager &) = delete;
    MediaManager &operator=(const MediaManager &) = delete;

    bool m_isDucked = false;
    QStringList m_pausedPlayers;
};

} // namespace Kity
