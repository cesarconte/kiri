/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>

namespace Kity {

class AudioManager : public QObject
{
    Q_OBJECT

public:
    static AudioManager &instance();

    // Volume & Sink control (PipeWire / WirePlumber wpctl)
    QString getAudioVolumeStatus() const;
    int getVolumePercent() const;
    bool isAudioMuted() const;
    bool setVolume(int percentage);
    bool stepVolume(int deltaPercentage);
    bool setAudioMute(bool mute);

    // Microphone & Source control
    QString getMicrophoneStatus() const;
    int getMicrophonePercent() const;
    bool isMicrophoneMuted() const;
    bool setMicrophoneMute(bool mute);

    // EasyEffects integration
    bool toggleEasyEffectsBypass();
    bool loadEasyEffectsPreset(const QString &presetName);

private:
    explicit AudioManager(QObject *parent = nullptr);
    ~AudioManager() override = default;

    AudioManager(const AudioManager &) = delete;
    AudioManager &operator=(const AudioManager &) = delete;
};

} // namespace Kity
