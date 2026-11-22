/*
    SPDX-FileCopyrightText: 2026 Cesar
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "whisperengine.h"

#include <ggml-backend.h>
#include <whisper.h>

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QStandardPaths>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <mutex>

static void ensureGgmlBackendsLoaded()
{
    static std::once_flag flag;
    std::call_once(flag, []() {
        ggml_backend_load_all();
    });
}

WhisperEngine::WhisperEngine(QObject *parent)
    : QObject(parent)
{
    ensureGgmlBackendsLoaded();
    m_threadPool.setMaxThreadCount(1);
}

WhisperEngine::~WhisperEngine()
{
    m_threadPool.waitForDone();
    QMutexLocker locker(&m_mutex);
    if (m_ctx) {
        whisper_free(m_ctx);
        m_ctx = nullptr;
    }
}

bool WhisperEngine::isModelLoaded() const
{
    return m_modelLoaded.load();
}

QString WhisperEngine::currentModelPath() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentModelPath;
}

void WhisperEngine::loadModelAsync(const QString &modelPath)
{
    m_threadPool.start([this, modelPath]() {
        loadModel(modelPath);
    });
}

QString WhisperEngine::findDefaultModel()
{
    const QString smallLocated = QStandardPaths::locate(
        QStandardPaths::GenericDataLocation,
        QStringLiteral("kity/models/ggml-small.bin"));
    if (!smallLocated.isEmpty() && QFileInfo::exists(smallLocated)) {
        return smallLocated;
    }

    const QString baseLocated = QStandardPaths::locate(
        QStandardPaths::GenericDataLocation,
        QStringLiteral("kity/models/ggml-base.bin"));
    if (!baseLocated.isEmpty() && QFileInfo::exists(baseLocated)) {
        return baseLocated;
    }

    const QString userSmall = QDir::homePath() + QStringLiteral("/.local/share/kity/models/ggml-small.bin");
    if (QFileInfo::exists(userSmall)) {
        return userSmall;
    }

    const QString userBase = QDir::homePath() + QStringLiteral("/.local/share/kity/models/ggml-base.bin");
    if (QFileInfo::exists(userBase)) {
        return userBase;
    }

    const QString sysSmall = QStringLiteral("/usr/share/kity/models/ggml-small.bin");
    if (QFileInfo::exists(sysSmall)) {
        return sysSmall;
    }

    const QString sysBase = QStringLiteral("/usr/share/kity/models/ggml-base.bin");
    if (QFileInfo::exists(sysBase)) {
        return sysBase;
    }

    return QString();
}

QStringList WhisperEngine::discoverModels()
{
    QStringList found;
    const QStringList searchDirs = {
        QDir::homePath() + QStringLiteral("/.local/share/kity/models"),
        QDir::homePath() + QStringLiteral("/.local/share/kdictation/models"),
        QStringLiteral("/usr/share/kity/models"),
        QStringLiteral("/usr/share/whisper-models")
    };

    for (const QString &dirPath : searchDirs) {
        QDir dir(dirPath);
        if (dir.exists()) {
            const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.bin"), QDir::Files);
            for (const QString &file : files) {
                const QString full = dir.absoluteFilePath(file);
                if (!found.contains(full)) {
                    found.append(full);
                }
            }
        }
    }
    return found;
}

bool WhisperEngine::loadModel(const QString &modelPath)
{
    if (modelPath.trimmed().isEmpty() || !QFileInfo::exists(modelPath)) {
        const QString err = QStringLiteral("Modelo Whisper no encontrado: ") + modelPath;
        Q_EMIT modelLoadFailed(err);
        return false;
    }

    ensureGgmlBackendsLoaded();

    QMutexLocker locker(&m_mutex);

    if (m_ctx && m_currentModelPath == modelPath) {
        return true;
    }

    if (m_ctx) {
        whisper_free(m_ctx);
        m_ctx = nullptr;
        m_modelLoaded.store(false);
    }

    whisper_context_params cparams = whisper_context_default_params();
    // Default to system-selected backend (GPU/Vulkan/CPU)
    cparams.use_gpu = true;
    cparams.flash_attn = true;

    whisper_context *newCtx = whisper_init_from_file_with_params(modelPath.toUtf8().constData(), cparams);
    if (!newCtx) {
        // Fallback with conservative params (CPU only) if GPU init failed
        cparams.use_gpu = false;
        cparams.flash_attn = false;
        newCtx = whisper_init_from_file_with_params(modelPath.toUtf8().constData(), cparams);
    }

    if (!newCtx) {
        const QString err = QStringLiteral("Error al inicializar whisper_context desde ") + modelPath;
        Q_EMIT modelLoadFailed(err);
        return false;
    }

    m_ctx = newCtx;
    m_currentModelPath = modelPath;
    m_modelLoaded.store(true);

    Q_EMIT modelLoaded(modelPath);
    return true;
}

std::vector<float> WhisperEngine::pcmS16ToFloat(const QByteArray &pcmS16Data)
{
    const size_t sampleCount = pcmS16Data.size() / sizeof(int16_t);
    std::vector<float> pcmf32(sampleCount);
    if (sampleCount == 0) {
        return pcmf32;
    }

    const auto *samples = reinterpret_cast<const int16_t *>(pcmS16Data.constData());
    for (size_t i = 0; i < sampleCount; ++i) {
        pcmf32[i] = static_cast<float>(samples[i]) / 32768.0f;
    }
    return pcmf32;
}

QString WhisperEngine::runInference(const std::vector<float> &pcmf32,
                                    const QString &language,
                                    const QString &initialPrompt)
{
    QMutexLocker locker(&m_mutex);
    if (!m_ctx || pcmf32.empty()) {
        return QString();
    }

    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_progress = false;
    wparams.print_special = false;
    wparams.print_realtime = false;
    wparams.print_timestamps = false;
    wparams.no_timestamps = true;
    wparams.suppress_blank = true;
    wparams.n_threads = std::clamp(QThread::idealThreadCount(), 1, 8);

    const QByteArray langBytes = language.trimmed().toUtf8();
    if (!langBytes.isEmpty()) {
        wparams.language = langBytes.constData();
    } else {
        wparams.language = "es";
    }

    const QByteArray promptBytes = initialPrompt.trimmed().toUtf8();
    if (!promptBytes.isEmpty()) {
        wparams.initial_prompt = promptBytes.constData();
    }

    const int ret = whisper_full(m_ctx, wparams, pcmf32.data(), static_cast<int>(pcmf32.size()));
    if (ret != 0) {
        return QString();
    }

    const int n_segments = whisper_full_n_segments(m_ctx);
    QString result;
    for (int i = 0; i < n_segments; ++i) {
        const char *text = whisper_full_get_segment_text(m_ctx, i);
        if (text) {
            result += QString::fromUtf8(text);
        }
    }
    return result.trimmed();
}

QString WhisperEngine::transcribeSync(const QByteArray &pcmS16Data,
                                      const QString &language,
                                      const QString &initialPrompt)
{
    if (!isModelLoaded()) {
        const QString defaultModel = findDefaultModel();
        if (defaultModel.isEmpty() || !loadModel(defaultModel)) {
            return QString();
        }
    }

    const std::vector<float> pcmf32 = pcmS16ToFloat(pcmS16Data);
    return runInference(pcmf32, language, initialPrompt);
}

void WhisperEngine::transcribeAsync(const QByteArray &pcmS16Data,
                                    const QString &language,
                                    const QString &initialPrompt)
{
    if (m_transcribing.exchange(true)) {
        Q_EMIT transcriptionFailed(QStringLiteral("Transcripción en curso, por favor espera"));
        return;
    }

    m_threadPool.start([this, pcmS16Data, language, initialPrompt]() {
        if (!isModelLoaded()) {
            const QString defaultModel = findDefaultModel();
            if (defaultModel.isEmpty() || !loadModel(defaultModel)) {
                m_transcribing.store(false);
                QMetaObject::invokeMethod(this, [this]() {
                    Q_EMIT transcriptionFailed(QStringLiteral("Modelo Whisper no disponible"));
                }, Qt::QueuedConnection);
                return;
            }
        }

        const std::vector<float> pcmf32 = pcmS16ToFloat(pcmS16Data);
        const QString transcript = runInference(pcmf32, language, initialPrompt);

        m_transcribing.store(false);

        QMetaObject::invokeMethod(this, [this, transcript]() {
            if (transcript.isEmpty()) {
                Q_EMIT transcriptionFailed(QStringLiteral("No se detectó voz en el audio"));
            } else {
                Q_EMIT transcriptionReady(transcript);
            }
        }, Qt::QueuedConnection);
    });
}
