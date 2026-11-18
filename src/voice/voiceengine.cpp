/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "voiceengine.h"

#include <cmath>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <KConfigGroup>
#include <KLocalizedString>
#include <KNotification>
#include <KSharedConfig>

namespace Kity {

namespace {

QString readConfigString(const QString &key, const QString &def = QString())
{
    const KSharedConfig::Ptr kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    const KConfigGroup cfg = kityrc->group(QStringLiteral("General"));
    return cfg.readEntry(key, def);
}

int readConfigInt(const QString &key, int def = 0)
{
    const KSharedConfig::Ptr kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    const KConfigGroup cfg = kityrc->group(QStringLiteral("General"));
    return cfg.readEntry(key, def);
}

bool readConfigBool(const QString &key, bool def = false)
{
    const KSharedConfig::Ptr kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    const KConfigGroup cfg = kityrc->group(QStringLiteral("General"));
    return cfg.readEntry(key, def);
}

QString resolveWhisperModelPath()
{
    QString path = readConfigString(QStringLiteral("whisperModelPath"));
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        const QStringList candidates = {
            QDir::homePath() + QStringLiteral("/.local/share/kity/models/ggml-base.bin"),
            QDir::homePath() + QStringLiteral("/.local/share/kity/models/ggml-small.bin"),
            QDir::homePath() + QStringLiteral("/.local/share/kity/models/ggml-tiny.bin")
        };
        for (const QString &cand : candidates) {
            if (QFileInfo::exists(cand)) {
                return cand;
            }
        }
    }
    return path;
}

QString resolveWhisperLanguage()
{
    return readConfigString(QStringLiteral("whisperLanguage"), QStringLiteral("es"));
}

QString resolveSpeechOutputMode()
{
    return readConfigString(QStringLiteral("speechOutputMode"), QStringLiteral("always"));
}

QString resolveGeminiModel()
{
    return readConfigString(QStringLiteral("geminiModel"), QStringLiteral("gemini-1.5-flash"));
}

} // anonymous namespace

VoiceEngine &VoiceEngine::instance()
{
    static VoiceEngine s_instance;
    return s_instance;
}

VoiceEngine::VoiceEngine(QObject *parent)
    : QObject(parent)
    , m_voiceRecorder(std::make_unique<PipeWireRecorder>())
    , m_wakeWordRecorder(std::make_unique<PipeWireRecorder>())
    , m_whisperEngine(std::make_unique<WhisperEngine>())
    , m_kdictationClient(std::make_unique<KDictationClient>(this))
    , m_geminiSttClient(std::make_unique<GeminiSttClient>(this))
    , m_ttsEngine(std::make_unique<TtsEngine>(this))
{
    connect(m_voiceRecorder.get(), &PipeWireRecorder::audioChunkReady, this, &VoiceEngine::onVoiceAudioChunkReady);
    connect(m_wakeWordRecorder.get(), &PipeWireRecorder::audioChunkReady, this, &VoiceEngine::onWakeWordAudioChunk);

    connect(m_whisperEngine.get(), &WhisperEngine::transcriptionReady, this, &VoiceEngine::onWhisperTranscriptionReady);
    connect(m_whisperEngine.get(), &WhisperEngine::transcriptionFailed, this, &VoiceEngine::onWhisperTranscriptionFailed);
    connect(m_whisperEngine.get(), &WhisperEngine::modelLoaded, this, [this](const QString &path) {
        qInfo() << "Kity::VoiceEngine: Warm Whisper model loaded in RAM:" << path;
        Q_EMIT whisperModelStatusChanged(true);
    });
    connect(m_whisperEngine.get(), &WhisperEngine::modelLoadFailed, this, [this](const QString &err) {
        qWarning() << "Kity::VoiceEngine: Failed to load Whisper model in RAM:" << err;
        Q_EMIT whisperModelStatusChanged(false);
    });

    connect(m_kdictationClient.get(), &KDictationClient::transcriptionReady, this, &VoiceEngine::onCentralizedTranscriptionReady);
    connect(m_kdictationClient.get(), &KDictationClient::transcriptionFailed, this, &VoiceEngine::onCentralizedTranscriptionFailed);
    connect(m_kdictationClient.get(), &KDictationClient::dictationInjected, this, &VoiceEngine::dictationInjected);
    connect(m_kdictationClient.get(), &KDictationClient::availabilityChanged, this, &VoiceEngine::kdictationAvailabilityChanged);

    connect(m_geminiSttClient.get(), &GeminiSttClient::transcriptionReady, this, [this](const QString &text) {
        QString cleaned = VoiceUtils::cleanWhisperOutput(text);
        cleaned = VoiceUtils::removeFillerWords(cleaned, QStringLiteral("es"));
        if (!cleaned.isEmpty()) {
            Q_EMIT transcriptionReady(cleaned, true);
        } else {
            runWhisperTranscription(m_recordedPcmBuffer);
        }
    });
    connect(m_geminiSttClient.get(), &GeminiSttClient::transcriptionFailed, this, [this](const QString &err) {
        qWarning() << "Kity: Gemini STT error:" << err << "- Falling back to WhisperEngine";
        runWhisperTranscription(m_recordedPcmBuffer);
    });

    connect(m_ttsEngine.get(), &TtsEngine::speakingStateChanged, this, &VoiceEngine::speakingStateChanged);
    connect(m_ttsEngine.get(), &TtsEngine::queueFinished, this, [this]() {
        const bool continuousVoice = readConfigBool(QStringLiteral("continuousConversation"), false);
        bool isBt = false;
        const QString captureSource = resolvePreferredCaptureSource(&isBt);
        if (m_lastQueryWasVoice && continuousVoice && !isBt && !captureSource.isEmpty()) {
            qInfo() << "Kity: Full-Duplex continuous conversational turn-taking active. Listening for natural follow-up...";
            startRecording();
        } else {
            m_isRecording = false;
            Q_EMIT recordingStateChanged(false);
        }
    });

    m_centralizedFallbackTimer = new QTimer(this);
    m_centralizedFallbackTimer->setSingleShot(true);
    connect(m_centralizedFallbackTimer, &QTimer::timeout, this, &VoiceEngine::onCentralizedSTTTimeout);

    m_vadTimer = new QTimer(this);
    connect(m_vadTimer, &QTimer::timeout, this, [this]() {
        if (!m_speechDetected || !m_speechSilenceTimer.isValid()) return;
        if (m_speechSilenceTimer.elapsed() > 1000) {
            stopVad();
            stopRecording();
        }
    });

    const KSharedConfig::Ptr kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    const KConfigGroup cfg = kityrc->group(QStringLiteral("General"));
    m_config.sttEngine = cfg.readEntry(QStringLiteral("sttEngine"), QStringLiteral("whisper"));
    m_config.whisperModelPath = cfg.readEntry(QStringLiteral("whisperModelPath"), QString());
    m_config.whisperLanguage = cfg.readEntry(QStringLiteral("whisperLanguage"), QStringLiteral("es"));
    m_config.piperVoicePath = cfg.readEntry(QStringLiteral("piperVoicePath"), QString());
    m_config.speechOutputMode = cfg.readEntry(QStringLiteral("speechOutputMode"), QStringLiteral("always"));
    m_config.vadEnabled = cfg.readEntry(QStringLiteral("vadEnabled"), true);
    m_config.earconsEnabled = cfg.readEntry(QStringLiteral("earconsEnabled"), true);
    m_config.useCentralizedVoice = cfg.readEntry(QStringLiteral("useCentralizedVoice"), false);
}

VoiceEngine::~VoiceEngine()
{
    stopSpeaking();
    stopRecording();
    stopWakeWordListener();
}

void VoiceEngine::setConfig(const VoiceConfig &config)
{
    m_config = config;
}

VoiceConfig VoiceEngine::config() const
{
    return m_config;
}

bool VoiceEngine::isRecording() const
{
    return m_isRecording;
}

qreal VoiceEngine::audioLevel() const
{
    return m_audioLevel;
}

bool VoiceEngine::isSpeaking() const
{
    return m_ttsEngine ? m_ttsEngine->isSpeaking() : false;
}

bool VoiceEngine::isWakeWordListening() const
{
    return m_wakeWordListening;
}

bool VoiceEngine::isPipeWireEchoCancelAvailable() const
{
    static bool checked = false;
    static bool available = false;
    if (!checked) {
        checked = true;
        const QString conf = QDir::homePath() + QStringLiteral("/.config/pipewire/pipewire.conf.d/20-echo-cancel.conf");
        available = QFile::exists(conf);
    }
    return available;
}

QString VoiceEngine::resolvePreferredCaptureSource(bool *isBluetoothOut) const
{
    if (isBluetoothOut) {
        *isBluetoothOut = false;
    }
    if (isPipeWireEchoCancelAvailable()) {
        return QStringLiteral("kity_echo_cancelled_source");
    }

    const QString pactl = QStandardPaths::findExecutable(QStringLiteral("pactl"));
    if (pactl.isEmpty()) {
        return QString();
    }

    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    proc.setProcessEnvironment(env);
    proc.start(pactl, QStringList() << QStringLiteral("list") << QStringLiteral("sources"));
    if (!proc.waitForFinished(1500)) {
        return QString();
    }

    const QString out = QString::fromUtf8(proc.readAllStandardOutput());
    QString alsaSource;
    QString fallbackSource;
    const bool sawBluetooth = out.contains(QLatin1String("bluez_"));

    QString currentName;
    QString activePort;
    QHash<QString, bool> unavailablePorts;
    bool inPortsSection = false;

    auto processBlock = [&]() {
        if (currentName.isEmpty() || currentName.endsWith(QLatin1String(".monitor")) || currentName.startsWith(QLatin1String("bluez_"))) {
            return;
        }
        const bool activePortUnavailable = !activePort.isEmpty() && unavailablePorts.value(activePort, false);
        if (currentName.startsWith(QLatin1String("alsa_input."))) {
            if (!activePortUnavailable && alsaSource.isEmpty()) {
                alsaSource = currentName;
            }
            return;
        }
        if (!activePortUnavailable && fallbackSource.isEmpty() && !currentName.contains(QLatin1String("easyeffects"))) {
            fallbackSource = currentName;
        }
    };

    const QStringList lines = out.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString trimmed = rawLine.trimmed();
        if (rawLine.startsWith(QLatin1String("Source #"))) {
            processBlock();
            currentName.clear();
            activePort.clear();
            unavailablePorts.clear();
            inPortsSection = false;
            continue;
        }
        if (trimmed.startsWith(QLatin1String("Name:"))) {
            currentName = trimmed.mid(5).trimmed();
            inPortsSection = false;
        } else if (trimmed == QLatin1String("Ports:")) {
            inPortsSection = true;
        } else if (trimmed.startsWith(QLatin1String("Active Port:"))) {
            activePort = trimmed.mid(12).trimmed();
            inPortsSection = false;
        } else if (inPortsSection) {
            if (!rawLine.startsWith(QLatin1String("\t\t"))) {
                inPortsSection = false;
            } else {
                const int colonIdx = trimmed.indexOf(QLatin1Char(':'));
                if (colonIdx > 0) {
                    const QString portName = trimmed.left(colonIdx).trimmed();
                    const bool isUnavailable = trimmed.contains(QLatin1String("not available)"));
                    unavailablePorts.insert(portName, isUnavailable);
                }
            }
        }
    }
    processBlock();

    if (isBluetoothOut) {
        *isBluetoothOut = sawBluetooth;
        if (sawBluetooth || alsaSource.isEmpty()) {
            return QString();
        }
    }
    return !alsaSource.isEmpty() ? alsaSource : fallbackSource;
}

void VoiceEngine::startRecording()
{
    if (m_isRecording) {
        return;
    }

    stopWakeWordListener();
    stopSpeaking();
    playSiriEarcon(true);

    const QString targetSource = resolvePreferredCaptureSource();
    if (!m_voiceRecorder->startRecording(16000, 1, targetSource, QStringLiteral("kity-voice-capture"))) {
        Q_EMIT transcriptionFailed(i18n("No se pudo iniciar la captura de audio con PipeWire nativo."));
        return;
    }

    m_speechDetected = false;
    m_speechSilenceTimer.invalidate();
    m_recordingDurationTimer.start();

    m_isRecording = true;
    Q_EMIT recordingStateChanged(true);
}

void VoiceEngine::stopRecording()
{
    m_speechSilenceTimer.invalidate();
    m_recordingDurationTimer.invalidate();

    QByteArray pcmData;
    if (m_voiceRecorder && m_voiceRecorder->isRecording()) {
        pcmData = m_voiceRecorder->stopRecording();
    }

    m_audioLevel = 0.0;
    const bool wasRecording = m_isRecording;
    m_isRecording = false;
    Q_EMIT recordingStateChanged(false);
    Q_EMIT audioLevelChanged(0.0);

    if (wasRecording) {
        playSiriEarcon(false);

        if (pcmData.size() < 3200) {
            return;
        }

        m_recordedPcmBuffer = pcmData;
        runSpeechTranscription(pcmData);
    }
}

void VoiceEngine::toggleRecording()
{
    if (m_isRecording) {
        stopRecording();
    } else {
        startRecording();
    }
}

void VoiceEngine::startVad()
{
    m_speechDetected = false;
    m_speechSilenceTimer.invalidate();
    m_recordingDurationTimer.start();
}

void VoiceEngine::stopVad()
{
    m_speechSilenceTimer.invalidate();
    m_recordingDurationTimer.invalidate();
    m_audioLevel = 0.0;
    Q_EMIT audioLevelChanged(0.0);
}

void VoiceEngine::onVoiceAudioChunkReady(const QByteArray &chunk, double level)
{
    Q_UNUSED(chunk);
    if (!m_isRecording) {
        return;
    }

    const double speechRmsThreshold = 0.025;
    const bool isSpeakingNow = (level >= speechRmsThreshold);

    if (isSpeakingNow) {
        m_speechDetected = true;
        m_speechSilenceTimer.start();
    }

    if (readConfigBool(QStringLiteral("vadEnabled"), true)) {
        const int silenceTimeout = readConfigInt(QStringLiteral("vadSilenceTimeoutMs"), 1300);

        if (m_speechDetected) {
            if (!isSpeakingNow && m_speechSilenceTimer.isValid() && m_speechSilenceTimer.elapsed() > silenceTimeout) {
                stopRecording();
            }
        } else {
            const double maxSilenceSeconds = 5.0;
            if (m_recordingDurationTimer.isValid() && m_recordingDurationTimer.elapsed() > static_cast<qint64>(maxSilenceSeconds * 1000)) {
                stopRecording();
            }
        }
    }
}

void VoiceEngine::runSpeechTranscription(const QByteArray &pcmData)
{
    const QByteArray data = pcmData.isEmpty() ? m_recordedPcmBuffer : pcmData;
    m_recordedPcmBuffer = data;

    const QString engine = m_config.sttEngine;
    const QString apiKey = m_config.geminiApiKey;

    if (engine == QLatin1String("gemini") && !apiKey.isEmpty()) {
        m_geminiSttClient->transcribe(data, apiKey, resolveGeminiModel());
        return;
    }

    const bool centralizedEnabled = m_config.useCentralizedVoice;
    const bool kdictationReady = m_kdictationClient && m_kdictationClient->checkAvailability();

    if (centralizedEnabled && kdictationReady) {
        m_waitingForCentralizedSTT = true;
        ++m_sttSessionId;
        m_centralizedFallbackTimer->start(500);
        m_kdictationClient->transcribePcmAsync(data, resolveWhisperLanguage());
        return;
    }

    runWhisperTranscription(data);
}

void VoiceEngine::runWhisperTranscription(const QByteArray &pcmData)
{
    if (pcmData.size() < 1024) {
        Q_EMIT transcriptionFailed(i18n("No se detectó audio de voz."));
        return;
    }

    if (!m_whisperEngine) {
        return;
    }

    if (!m_whisperEngine->isModelLoaded()) {
        const QString modelPath = resolveWhisperModelPath();
        if (modelPath.isEmpty() || !m_whisperEngine->loadModel(modelPath)) {
            return;
        }
    }

    m_whisperEngine->transcribeAsync(pcmData, resolveWhisperLanguage());
}

void VoiceEngine::onWhisperTranscriptionReady(const QString &rawTranscript)
{
    QString cleaned = VoiceUtils::cleanWhisperOutput(rawTranscript);
    cleaned = VoiceUtils::removeFillerWords(cleaned, resolveWhisperLanguage());

    if (cleaned.isEmpty()) {
        return;
    }

    Q_EMIT transcriptionReady(cleaned, true);
}

void VoiceEngine::onWhisperTranscriptionFailed(const QString &errorMessage)
{
    qWarning() << "Kity::VoiceEngine: Whisper transcription failed:" << errorMessage;
    Q_EMIT transcriptionFailed(errorMessage);
}

void VoiceEngine::onCentralizedTranscriptionReady(const QString &rawTranscript)
{
    if (!m_waitingForCentralizedSTT) {
        return;
    }

    m_waitingForCentralizedSTT = false;
    if (m_centralizedFallbackTimer && m_centralizedFallbackTimer->isActive()) {
        m_centralizedFallbackTimer->stop();
    }

    qInfo() << "Kity: Transcripción recibida exitosamente desde KDictation vía D-Bus:" << rawTranscript;

    const QString cleaned = rawTranscript.trimmed();
    if (cleaned.isEmpty()) {
        return;
    }

    Q_EMIT transcriptionReady(cleaned, true);
}

void VoiceEngine::onCentralizedTranscriptionFailed(const QString &errorMessage)
{
    if (!m_waitingForCentralizedSTT) {
        return;
    }

    m_waitingForCentralizedSTT = false;
    if (m_centralizedFallbackTimer && m_centralizedFallbackTimer->isActive()) {
        m_centralizedFallbackTimer->stop();
    }

    qWarning() << "Kity: Error en llamada a KDictation (" << errorMessage << "). Activando fallback local a WhisperEngine…";
    runWhisperTranscription(m_recordedPcmBuffer);
}

void VoiceEngine::onCentralizedSTTTimeout()
{
    if (!m_waitingForCentralizedSTT) {
        return;
    }

    m_waitingForCentralizedSTT = false;
    qInfo() << "Kity: Timeout de 500 ms esperando a KDictation D-Bus. Conmutando transparentemente a WhisperEngine local…";
    runWhisperTranscription(m_recordedPcmBuffer);
}

void VoiceEngine::speakText(const QString &text)
{
    if (m_ttsEngine) {
        m_ttsEngine->speakText(text);
    }
}

void VoiceEngine::stopSpeaking()
{
    m_bargeInVoiceFrames = 0;
    if (m_ttsEngine) {
        m_ttsEngine->stopSpeaking();
    }
}

void VoiceEngine::processStreamingTtsChunk(const QString &newChunk)
{
    if (m_ttsEngine) {
        m_ttsEngine->processStreamingChunk(newChunk, resolveSpeechOutputMode(), m_lastQueryWasVoice);
    }
}

void VoiceEngine::flushStreamingTtsChunk()
{
    if (m_ttsEngine) {
        m_ttsEngine->flushStreamingBuffer();
    }
}

void VoiceEngine::enqueueSentenceForSpeech(const QString &sentence)
{
    if (m_ttsEngine) {
        m_ttsEngine->enqueueSentence(sentence);
    }
}

void VoiceEngine::setLastQueryWasVoice(bool wasVoice)
{
    m_lastQueryWasVoice = wasVoice;
}

bool VoiceEngine::lastQueryWasVoice() const
{
    return m_lastQueryWasVoice;
}

void VoiceEngine::reloadConfig()
{
    const KSharedConfig::Ptr kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    kityrc->reparseConfiguration();
    const KConfigGroup cfg = kityrc->group(QStringLiteral("General"));
    m_config.sttEngine = cfg.readEntry(QStringLiteral("sttEngine"), QStringLiteral("whisper"));
    m_config.whisperModelPath = cfg.readEntry(QStringLiteral("whisperModelPath"), QString());
    m_config.whisperLanguage = cfg.readEntry(QStringLiteral("whisperLanguage"), QStringLiteral("es"));
    m_config.piperVoicePath = cfg.readEntry(QStringLiteral("piperVoicePath"), QString());
    m_config.speechOutputMode = cfg.readEntry(QStringLiteral("speechOutputMode"), QStringLiteral("always"));
    m_config.vadEnabled = cfg.readEntry(QStringLiteral("vadEnabled"), true);
    m_config.earconsEnabled = cfg.readEntry(QStringLiteral("earconsEnabled"), true);
    m_config.useCentralizedVoice = cfg.readEntry(QStringLiteral("useCentralizedVoice"), false);
}

void VoiceEngine::transcribePcm(const QByteArray &pcmData)
{
    runSpeechTranscription(pcmData);
}

void VoiceEngine::transcribeWavFile(const QString &wavPath)
{
    QFile file(wavPath);
    if (!file.open(QIODevice::ReadOnly)) {
        Q_EMIT transcriptionFailed(i18n("No se pudo abrir el archivo WAV: %1", wavPath));
        return;
    }

    QByteArray data = file.readAll();
    if (data.size() > 44) {
        data = data.mid(44); // Strip 44-byte WAV header
    }
    transcribePcm(data);
}

void VoiceEngine::injectDictationPcm(const QByteArray &pcmData)
{
    m_lastQueryWasVoice = true;
    transcribePcm(pcmData);
}

void VoiceEngine::injectDictationText(const QString &text)
{
    m_lastQueryWasVoice = true;
    Q_EMIT transcriptionReady(text, true);
}

bool VoiceEngine::isWhisperModelLoaded() const
{
    return m_whisperEngine && m_whisperEngine->isModelLoaded();
}

bool VoiceEngine::isKDictationAvailable() const
{
    return m_kdictationClient && m_kdictationClient->isServiceAvailable();
}

void VoiceEngine::checkKDictationAvailability()
{
    if (m_kdictationClient) {
        m_kdictationClient->checkAvailability();
    }
}

void VoiceEngine::preloadWhisperModel(const QString &modelPath)
{
    if (m_whisperEngine && !modelPath.isEmpty()) {
        m_whisperEngine->loadModelAsync(modelPath);
    }
}

void VoiceEngine::startWakeWordListener()
{
    m_wakeWordListening = true;

    if (m_isRecording) {
        return;
    }

    if (m_wakeWordRecorder && m_wakeWordRecorder->isRecording()) {
        return;
    }

    bool isBtOnly = false;
    const QString targetSource = resolvePreferredCaptureSource(&isBtOnly);
    if (targetSource.isEmpty()) {
        qInfo() << "Kity: Skipping background wake word listener because no active non-Bluetooth capture source is available (bluetoothPresent="
                << isBtOnly << ")";
        return;
    }

    m_wakeWordPcmBuffer.clear();
    m_wakeWordEvaluating = false;
    m_bargeInVoiceFrames = 0;

    if (!m_wakeWordRecorder->startRecording(16000, 1, targetSource, QStringLiteral("kity-wake-word"))) {
        qWarning() << "Kity: Failed to start PipeWire wake word listener";
    } else {
        qInfo() << "Kity: PipeWire native wake word & Barge-in background listener started successfully";
    }
}

void VoiceEngine::stopWakeWordListener()
{
    if (m_wakeWordRecorder && m_wakeWordRecorder->isRecording()) {
        m_wakeWordRecorder->stopRecording();
    }
    m_wakeWordPcmBuffer.clear();
    m_wakeWordEvaluating = false;
    m_bargeInVoiceFrames = 0;
}

void VoiceEngine::onWakeWordAudioChunk(const QByteArray &chunk, double rms)
{
    if (chunk.isEmpty()) return;

    m_wakeWordPcmBuffer.append(chunk);

    // Keep circular buffer to ~2.5 seconds (16000 samples/sec * 2 bytes = 32000 B/s -> 80000 B)
    if (m_wakeWordPcmBuffer.size() > 80000) {
        m_wakeWordPcmBuffer = m_wakeWordPcmBuffer.right(80000);
    }

    // Live Full-Duplex Barge-in detection
    if (isSpeaking()) {
        const double bargeInThreshold = isPipeWireEchoCancelAvailable() ? 0.026 : 0.045;
        if (rms >= bargeInThreshold) {
            m_bargeInVoiceFrames++;
            if (m_bargeInVoiceFrames >= 2) {
                qInfo() << "Kity: Live Full-Duplex Barge-in triggered! Aborting speech.";
                m_bargeInVoiceFrames = 0;
                stopSpeaking();
                startRecording();
                return;
            }
        } else {
            m_bargeInVoiceFrames = 0;
        }
        return;
    }

    if (m_wakeWordEvaluating) return;

    if (rms >= 0.016 && (!m_wakeWordCooldown.isValid() || m_wakeWordCooldown.elapsed() > 1200)) {
        if (m_wakeWordPcmBuffer.size() >= 24000) {
            m_wakeWordEvaluating = true;
            qInfo() << "Kity: Speech detected in background stream (RMS:" << rms << "), capturing wake word window...";

            QTimer::singleShot(650, this, [this]() {
                if (m_isRecording) {
                    m_wakeWordEvaluating = false;
                    return;
                }
                const QByteArray pcmData = m_wakeWordPcmBuffer.right(64000);
                checkWakeWordTranscription(pcmData);
            });
        }
    }
}

void VoiceEngine::checkWakeWordTranscription(const QByteArray &pcmData)
{
    if (pcmData.size() < 1024 || !m_whisperEngine) {
        m_wakeWordEvaluating = false;
        return;
    }

    if (!m_whisperEngine->isModelLoaded()) {
        const QString modelPath = resolveWhisperModelPath();
        if (modelPath.isEmpty() || !m_whisperEngine->loadModel(modelPath)) {
            m_wakeWordEvaluating = false;
            return;
        }
    }

    const QString rawTranscript = m_whisperEngine->transcribeSync(
        pcmData,
        QStringLiteral("es"),
        QStringLiteral("Hola Kity, Oye Kity, Hey Kity"));

    m_wakeWordEvaluating = false;
    m_wakeWordCooldown.start();

    QString cleaned = rawTranscript.trimmed();
    static const QRegularExpression bracketRegex(QStringLiteral("\\[.*?\\]|\\(.*?\\)"));
    cleaned.remove(bracketRegex);
    cleaned = cleaned.trimmed().toLower();

    qInfo() << "Kity: Wake word probe transcription result (Warm Engine):" << cleaned;

    static const QRegularExpression wwRegex(
        QStringLiteral("(?:hola|oye|hey|ok|quiti|kiti|quity|kitty)?\\s*(?:kity|kitty)|hola\\s*(?:quiti|kiti)"),
        QRegularExpression::CaseInsensitiveOption);

    if (cleaned.contains(wwRegex) || cleaned.contains(QStringLiteral("kity")) || cleaned.contains(QStringLiteral("kitty")) || cleaned.contains(QStringLiteral("quiti")) || cleaned.contains(QStringLiteral("kiti"))) {
        qInfo() << "Kity: WAKE WORD ACTIVATED! ('" << cleaned << "')";

        KNotification::event(QStringLiteral("notification"),
                             i18n("Kity"),
                             i18n("¡Hola! Te escucho…"));

        stopWakeWordListener();
        Q_EMIT wakeWordTriggered();

        QTimer::singleShot(250, this, [this]() {
            startRecording();
        });
    }
}

void VoiceEngine::playSiriEarcon(bool activating)
{
    if (m_ttsEngine) {
        m_ttsEngine->playSiriEarcon(activating, readConfigBool(QStringLiteral("earconsEnabled"), true));
    }
}

} // namespace Kity
