/*
    SPDX-FileCopyrightText: 2026 Cesar
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "pipewirerecorder.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#pragma GCC diagnostic pop

#include <QDataStream>
#include <QFile>
#include <QMetaObject>

#include <algorithm>
#include <cmath>
#include <mutex>

static std::atomic<bool> s_pipewireInitialized{false};

static void ensurePipeWireInitialized()
{
    static std::once_flag initFlag;
    std::call_once(initFlag, []() {
        pw_init(nullptr, nullptr);
        s_pipewireInitialized.store(true);
    });
}

PipeWireRecorder::PipeWireRecorder(QObject *parent)
    : QObject(parent)
{
    ensurePipeWireInitialized();
}

PipeWireRecorder::~PipeWireRecorder()
{
    if (m_recording.load()) {
        stopRecording();
    }
}

bool PipeWireRecorder::isRecording() const
{
    return m_recording.load();
}

double PipeWireRecorder::currentAudioLevel() const
{
    return m_currentAudioLevel.load();
}

double PipeWireRecorder::calculateRmsLevel(const int16_t *samples, size_t sampleCount)
{
    if (!samples || sampleCount == 0) {
        return 0.0;
    }

    double sumSquares = 0.0;
    for (size_t i = 0; i < sampleCount; ++i) {
        const double norm = static_cast<double>(samples[i]) / 32768.0;
        sumSquares += norm * norm;
    }

    const double rms = std::sqrt(sumSquares / static_cast<double>(sampleCount));
    return std::clamp(rms * 5.0, 0.0, 1.0);
}

void PipeWireRecorder::onStreamProcess(void *userData)
{
    auto *self = static_cast<PipeWireRecorder *>(userData);
    if (self) {
        self->handleProcessCallback();
    }
}

void PipeWireRecorder::onStreamStateChanged(void *userData, int, int newState, const char *error)
{
    auto *self = static_cast<PipeWireRecorder *>(userData);
    if (!self) {
        return;
    }

    if (newState == PW_STREAM_STATE_ERROR) {
        const QString errStr = error ? QString::fromUtf8(error) : QStringLiteral("Error desconocido en stream de PipeWire");
        QMetaObject::invokeMethod(self, [self, errStr]() {
            Q_EMIT self->errorOccurred(errStr);
        }, Qt::QueuedConnection);
    }
}

void PipeWireRecorder::handleProcessCallback()
{
    if (!m_stream) {
        return;
    }

    pw_buffer *b = pw_stream_dequeue_buffer(m_stream);
    if (!b) {
        return;
    }

    spa_buffer *buf = b->buffer;
    if (buf && buf->datas[0].data != nullptr) {
        const uint32_t size = buf->datas[0].chunk->size;
        const uint32_t offset = buf->datas[0].chunk->offset;
        const auto *src = static_cast<const uint8_t *>(buf->datas[0].data) + offset;

        if (size > 0) {
            QByteArray chunk(reinterpret_cast<const char *>(src), static_cast<qsizetype>(size));
            {
                QMutexLocker locker(&m_bufferMutex);
                m_capturedPcm.append(chunk);
            }

            const auto *samples = reinterpret_cast<const int16_t *>(src);
            const size_t sampleCount = size / sizeof(int16_t);
            const double level = calculateRmsLevel(samples, sampleCount);
            m_currentAudioLevel.store(level);

            const auto now = std::chrono::steady_clock::now();
            const bool shouldNotifyLevel =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastLevelNotificationTime).count() >= 30;

            if (shouldNotifyLevel) {
                m_lastLevelNotificationTime = now;
            }

            QMetaObject::invokeMethod(this, [this, chunk, level, shouldNotifyLevel]() {
                Q_EMIT audioChunkReady(chunk, level);
                if (shouldNotifyLevel) {
                    Q_EMIT audioLevelChanged(level);
                }
            }, Qt::QueuedConnection);
        }
    }

    pw_stream_queue_buffer(m_stream, b);
}

bool PipeWireRecorder::startRecording(uint32_t sampleRate,
                                       uint32_t channels,
                                       const QString &targetNode,
                                       const QString &streamName)
{
    if (m_recording.load()) {
        return true;
    }

    m_sampleRate = sampleRate;
    m_channels = channels;

    {
        QMutexLocker locker(&m_bufferMutex);
        m_capturedPcm.clear();
    }
    m_currentAudioLevel.store(0.0);
    m_lastLevelNotificationTime = std::chrono::steady_clock::now();

    const QByteArray threadLoopName = (streamName + QStringLiteral("-loop")).toUtf8();
    m_loop = pw_thread_loop_new(threadLoopName.constData(), nullptr);
    if (!m_loop) {
        Q_EMIT errorOccurred(QStringLiteral("No se pudo crear el bucle de hilos de PipeWire"));
        return false;
    }

    if (pw_thread_loop_start(m_loop) < 0) {
        Q_EMIT errorOccurred(QStringLiteral("No se pudo iniciar el bucle de PipeWire"));
        pw_thread_loop_destroy(m_loop);
        m_loop = nullptr;
        return false;
    }

    pw_thread_loop_lock(m_loop);

    const QByteArray desc = (streamName + QStringLiteral(" Native Capture")).toUtf8();
    pw_properties *props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "Voice",
        PW_KEY_APP_NAME, "Kity",
        PW_KEY_NODE_DESCRIPTION, desc.constData(),
        nullptr);

    if (!targetNode.trimmed().isEmpty()) {
        pw_properties_set(props, PW_KEY_TARGET_OBJECT, targetNode.toUtf8().constData());
    }

    static pw_stream_events events = {};
    events.version = PW_VERSION_STREAM_EVENTS;
    events.process = onStreamProcess;
    events.state_changed = reinterpret_cast<void(*)(void*, enum pw_stream_state, enum pw_stream_state, const char*)>(onStreamStateChanged);

    const QByteArray streamNameBytes = streamName.toUtf8();
    m_stream = pw_stream_new_simple(
        pw_thread_loop_get_loop(m_loop),
        streamNameBytes.constData(),
        props,
        &events,
        this);

    if (!m_stream) {
        pw_thread_loop_unlock(m_loop);
        pw_thread_loop_stop(m_loop);
        pw_thread_loop_destroy(m_loop);
        m_loop = nullptr;
        Q_EMIT errorOccurred(QStringLiteral("No se pudo crear el stream de PipeWire"));
        return false;
    }

    uint8_t buffer[1024];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_audio_info_raw info = {};
    info.format = SPA_AUDIO_FORMAT_S16_LE;
    info.channels = m_channels;
    info.rate = m_sampleRate;
    const spa_pod *params[1];
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);
#pragma GCC diagnostic pop

    const int res = pw_stream_connect(
        m_stream,
        PW_DIRECTION_INPUT,
        PW_ID_ANY,
        static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
        params, 1);

    pw_thread_loop_unlock(m_loop);

    if (res < 0) {
        pw_thread_loop_stop(m_loop);
        pw_thread_loop_destroy(m_loop);
        m_loop = nullptr;
        m_stream = nullptr;
        Q_EMIT errorOccurred(QStringLiteral("Error al conectar el stream de PipeWire: ") + QString::number(res));
        return false;
    }

    m_recording.store(true);
    Q_EMIT stateChanged(true);
    return true;
}

QByteArray PipeWireRecorder::stopRecording()
{
    if (!m_recording.load()) {
        QMutexLocker locker(&m_bufferMutex);
        return m_capturedPcm;
    }

    m_recording.store(false);
    m_currentAudioLevel.store(0.0);

    if (m_loop && m_stream) {
        pw_thread_loop_lock(m_loop);
        pw_stream_destroy(m_stream);
        m_stream = nullptr;
        pw_thread_loop_unlock(m_loop);

        pw_thread_loop_stop(m_loop);
        pw_thread_loop_destroy(m_loop);
        m_loop = nullptr;
    }

    Q_EMIT stateChanged(false);

    QMutexLocker locker(&m_bufferMutex);
    return m_capturedPcm;
}

QByteArray PipeWireRecorder::pcmToWav(const QByteArray &pcmData, uint32_t sampleRate, uint16_t channels)
{
    const quint32 dataSize = static_cast<quint32>(pcmData.size());
    const quint32 chunkSize = 36 + dataSize;
    const quint16 bitsPerSample = 16;
    const quint32 byteRate = sampleRate * channels * (bitsPerSample / 8);
    const quint16 blockAlign = channels * (bitsPerSample / 8);

    QByteArray wav;
    wav.reserve(44 + pcmData.size());
    QDataStream stream(&wav, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    // RIFF header
    stream.writeRawData("RIFF", 4);
    stream << chunkSize;
    stream.writeRawData("WAVE", 4);

    // fmt subchunk
    stream.writeRawData("fmt ", 4);
    stream << quint32(16);          // Subchunk1Size (16 for PCM)
    stream << quint16(1);           // AudioFormat (1 = PCM)
    stream << channels;
    stream << sampleRate;
    stream << byteRate;
    stream << blockAlign;
    stream << bitsPerSample;

    // data subchunk
    stream.writeRawData("data", 4);
    stream << dataSize;

    wav.append(pcmData);
    return wav;
}

bool PipeWireRecorder::savePcmToWavFile(const QString &filePath,
                                        const QByteArray &pcmData,
                                        uint32_t sampleRate,
                                        uint16_t channels)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    const QByteArray wav = pcmToWav(pcmData, sampleRate, channels);
    return file.write(wav) == wav.size();
}
