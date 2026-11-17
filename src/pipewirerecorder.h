/*
    SPDX-FileCopyrightText: 2026 Cesar
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QString>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>

struct pw_thread_loop;
struct pw_stream;

class PipeWireRecorder : public QObject
{
    Q_OBJECT

public:
    explicit PipeWireRecorder(QObject *parent = nullptr);
    ~PipeWireRecorder() override;

    bool startRecording(uint32_t sampleRate = 16000,
                        uint32_t channels = 1,
                        const QString &targetNode = QString(),
                        const QString &streamName = QStringLiteral("kity-voice-capture"));
    QByteArray stopRecording();

    bool isRecording() const;
    double currentAudioLevel() const;

    static double calculateRmsLevel(const int16_t *samples, size_t sampleCount);
    static QByteArray pcmToWav(const QByteArray &pcmData,
                               uint32_t sampleRate = 16000,
                               uint16_t channels = 1);
    static bool savePcmToWavFile(const QString &filePath,
                                 const QByteArray &pcmData,
                                 uint32_t sampleRate = 16000,
                                 uint16_t channels = 1);

Q_SIGNALS:
    void audioLevelChanged(double level);
    void audioChunkReady(const QByteArray &chunk, double level);
    void stateChanged(bool recording);
    void errorOccurred(const QString &errorMessage);

private:
    static void onStreamProcess(void *userData);
    static void onStreamStateChanged(void *userData, int oldState, int newState, const char *error);
    void handleProcessCallback();

    pw_thread_loop *m_loop = nullptr;
    pw_stream *m_stream = nullptr;

    mutable QMutex m_bufferMutex;
    QByteArray m_capturedPcm;

    std::atomic<bool> m_recording{false};
    std::atomic<double> m_currentAudioLevel{0.0};
    std::chrono::steady_clock::time_point m_lastLevelNotificationTime;

    uint32_t m_sampleRate = 16000;
    uint32_t m_channels = 1;
};
