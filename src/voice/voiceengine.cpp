/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "voiceengine.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>
#include <KLocalizedString>
#include <KSharedConfig>
#include <KConfigGroup>
#include <KNotification>

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

double readConfigDouble(const QString &key, double def = 0.0)
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

QString resolvePiperVoicePath()
{
    QString path = readConfigString(QStringLiteral("piperVoicePath"));
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        const QStringList candidates = {
            QDir::homePath() + QStringLiteral("/.local/share/kity/models/es_ES-davefx-medium.onnx"),
            QDir::homePath() + QStringLiteral("/.local/share/kity/models/es_ES-sharvard-medium.onnx")
        };
        for (const QString &cand : candidates) {
            if (QFileInfo::exists(cand)) {
                return cand;
            }
        }
    }
    return path;
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

    m_nam = new QNetworkAccessManager(this);
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
    return m_isSpeakingSentence || !m_ttsQueue.isEmpty() || (m_pwPlayProcess && m_pwPlayProcess->state() == QProcess::Running);
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
        if (currentName.isEmpty()) {
            return;
        }
        if (currentName.endsWith(QLatin1String(".monitor"))) {
            return;
        }
        if (currentName.startsWith(QLatin1String("bluez_"))) {
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
        // Never hijack or lock Bluetooth devices with a continuous background wake word listener,
        // and never run background wake word listener if no physical hardware microphone is connected!
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
    m_isRecording = false;
    Q_EMIT recordingStateChanged(false);
    Q_EMIT audioLevelChanged(0.0);

    if (m_isRecording) {
        playSiriEarcon(false);

        if (pcmData.size() < 3200) { // Menos de 100ms
            m_isRecording = false;
            Q_EMIT recordingStateChanged(false);
            return;
        }

        m_recordedPcmBuffer = pcmData;
        // transcribing
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

    // Voice Activity Detection (audible speech typically > 0.025 RMS)
    const double speechRmsThreshold = 0.025;
    const bool isSpeakingNow = (level >= speechRmsThreshold);

    if (isSpeakingNow) {
        m_speechDetected = true;
        // User is currently speaking: reset the silence countdown
        m_speechSilenceTimer.start();
    }

    if (readConfigBool(QStringLiteral("vadEnabled"), true)) {
        const int silenceTimeout = readConfigInt(QStringLiteral("vadSilenceTimeoutMs"), 1300);

        if (m_speechDetected) {
            // After speech was detected, stop once silence extends beyond silenceTimeout
            if (!isSpeakingNow && m_speechSilenceTimer.isValid() && m_speechSilenceTimer.elapsed() > silenceTimeout) {
                stopRecording();
            }
        } else {
            // Safety guard: stop after 4.5 seconds if in conversational follow-up, or 7 seconds initially
            const double maxSilenceSeconds = 5.0;
            if (m_recordingDurationTimer.isValid() && m_recordingDurationTimer.elapsed() > static_cast<qint64>(maxSilenceSeconds * 1000)) {
                stopRecording();
                m_isRecording = false;
    Q_EMIT recordingStateChanged(false);
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
        runGeminiAudioTranscription(data);
        return;
    }

    // Pipeline Híbrido con KDictation centralizado vía D-Bus
    const bool centralizedEnabled = m_config.useCentralizedVoice;
    const bool kdictationReady = m_kdictationClient && m_kdictationClient->checkAvailability();

    if (centralizedEnabled && kdictationReady) {
        m_waitingForCentralizedSTT = true;
        ++m_sttSessionId;

        // transcribing

        // Timeout corto de 500 ms: si KDictation no responde a tiempo, conmutar a Whisper local
        m_centralizedFallbackTimer->start(500);

        m_kdictationClient->transcribePcmAsync(data, resolveWhisperLanguage());
        return;
    }

    // Fallback inmediato a WhisperEngine local en RAM
    runWhisperTranscription(data);
}




void VoiceEngine::runGeminiAudioTranscription(const QByteArray &pcmData)
{
    if (pcmData.size() < 1024) {
        Q_EMIT transcriptionFailed(i18n("No se detectó audio de voz."));
        return;
    }

    const QString apiKey = m_config.geminiApiKey;
    if (apiKey.isEmpty()) {
        runWhisperTranscription(pcmData);
        return;
    }

    // transcribing

    const QByteArray wavData = PipeWireRecorder::pcmToWav(pcmData, 16000, 1);
    const QString base64Audio = QString::fromLatin1(wavData.toBase64());
    const QString geminiModel = resolveGeminiModel();
    const QString modelToUse = (geminiModel.isEmpty() ? QStringLiteral("gemini-2.0-flash") : geminiModel);

    const QUrl geminiUrl(QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent?key=%2")
        .arg(modelToUse, apiKey));

    QJsonObject audioPart;
    QJsonObject inlineData;
    inlineData[QStringLiteral("mime_type")] = QStringLiteral("audio/wav");
    inlineData[QStringLiteral("data")] = base64Audio;
    audioPart[QStringLiteral("inline_data")] = inlineData;

    QJsonObject promptPart;
    promptPart[QStringLiteral("text")] = QStringLiteral("Transcribe exactamente este audio de voz a texto en español. Responde exclusivamente con las palabras transcritas, sin explicaciones, sin preámbulos y sin comillas.");

    QJsonArray parts;
    parts.append(audioPart);
    parts.append(promptPart);

    QJsonObject contentObj;
    contentObj[QStringLiteral("parts")] = parts;

    QJsonArray contentsArr;
    contentsArr.append(contentObj);

    QJsonObject genConfig;
    genConfig[QStringLiteral("temperature")] = 0.0;

    QJsonObject payload;
    payload[QStringLiteral("contents")] = contentsArr;
    payload[QStringLiteral("generationConfig")] = genConfig;

    QNetworkRequest req(geminiUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(8000);

    QNetworkReply *reply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, pcmData]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "Kity: Gemini audio transcription error:" << reply->errorString()
                       << "- Falling back to local Whisper.cpp";
            runWhisperTranscription(pcmData);
            return;
        }

        const QByteArray responseData = reply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(responseData);
        const QJsonObject root = doc.object();
        const QJsonArray candidates = root.value(QStringLiteral("candidates")).toArray();
        if (candidates.isEmpty()) {
            qWarning() << "Kity: Gemini returned no candidates - Falling back to local Whisper.cpp";
            runWhisperTranscription(pcmData);
            return;
        }

        const QJsonObject firstCand = candidates.first().toObject();
        const QJsonObject content = firstCand.value(QStringLiteral("content")).toObject();
        const QJsonArray candParts = content.value(QStringLiteral("parts")).toArray();
        QString text;
        for (const QJsonValue &pv : candParts) {
            text += pv.toObject().value(QStringLiteral("text")).toString();
        }
        QString cleaned = cleanWhisperOutput(text);
        cleaned = removeFillerWords(cleaned, QStringLiteral("es"));

        if (cleaned.isEmpty()) {
            qWarning() << "Kity: Gemini transcription text was empty - Falling back to local Whisper.cpp";
            runWhisperTranscription(pcmData);
            return;
        }

        Q_EMIT transcriptionReady(cleaned, true);
        
    });
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
    QString cleaned = cleanWhisperOutput(rawTranscript);
    cleaned = removeFillerWords(cleaned, resolveWhisperLanguage());

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




QString VoiceEngine::cleanWhisperOutput(const QString &raw)
{
    QStringList keptLines;
    static const QRegularExpression timestampRe(
        QStringLiteral(R"(^\s*\[[0-9:.]+\s*-->\s*[0-9:.]+\]\s*)"));
    static const QRegularExpression bracketTagRe(
        QStringLiteral(R"(\[[^\]]*\])"));
    static const QRegularExpression parenTagRe(
        QStringLiteral(R"(\([^\)]*\))"));

    const QStringList lines = raw.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line.remove(timestampRe);
        line.remove(bracketTagRe);
        line.remove(parenTagRe);
        line = line.trimmed();
        if (!line.isEmpty()) {
            keptLines.append(line);
        }
    }
    return keptLines.join(QLatin1Char(' ')).simplified();
}




QString VoiceEngine::removeFillerWords(const QString &text, const QString &lang)
{
    if (text.trimmed().isEmpty()) {
        return QString();
    }

    QString out = text;

    if (lang.startsWith(QLatin1String("es"), Qt::CaseInsensitive)) {
        // 1. Eliminar repetición accidental de palabras contiguas (tartamudeo: "el el", "vamos vamos")
        static const QRegularExpression stutterRe(
            QStringLiteral(R"(\b([a-zA-ZáéíóúüñÁÉÍÓÚÜÑ]+)\s+\1\b)"),
            QRegularExpression::CaseInsensitiveOption);
        out.replace(stutterRe, QStringLiteral("\\1"));

        // 2. Muletillas y sonidos de titubeo (eh, em, este, mmm, ah)
        static const QRegularExpression hesitationRe(
            QStringLiteral(R"(\b(?:eh+|em+|este+|mmm+|ah+)\b)"),
            QRegularExpression::CaseInsensitiveOption);
        out.replace(hesitationRe, QStringLiteral(""));

        // 3. Muletillas conversacionales al inicio de frase o encadenadas
        static const QRegularExpression starterFillerRe(
            QStringLiteral(R"(^(?:\s*(?:bueno|pues|o sea|en plan|digamos que)[,\s]*)+)"),
            QRegularExpression::CaseInsensitiveOption);
        out.replace(starterFillerRe, QStringLiteral(""));

        static const QRegularExpression inlineFillerRe(
            QStringLiteral(R"((?:,\s*|\s+)(?:o sea|en plan)(?:,\s*|\s+))"),
            QRegularExpression::CaseInsensitiveOption);
        out.replace(inlineFillerRe, QStringLiteral(", "));
    } else if (lang.startsWith(QLatin1String("en"), Qt::CaseInsensitive)) {
        static const QRegularExpression stutterRe(
            QStringLiteral(R"(\b([a-zA-Z]+)\s+\1\b)"),
            QRegularExpression::CaseInsensitiveOption);
        out.replace(stutterRe, QStringLiteral("\\1"));

        static const QRegularExpression hesitationRe(
            QStringLiteral(R"(\b(?:uh+|um+|er+|ah+|like,?\s+|you know,?\s+)\b)"),
            QRegularExpression::CaseInsensitiveOption);
        out.replace(hesitationRe, QStringLiteral(""));
    }

    // Normalizar signos y espacios residuales
    static const QRegularExpression orphanLeadingPunctRe(QStringLiteral(R"(^[,\s;:.-]+)"));
    out.remove(orphanLeadingPunctRe);
    static const QRegularExpression multiSpaceRe(QStringLiteral(R"([ \t]{2,})"));
    out.replace(multiSpaceRe, QStringLiteral(" "));
    static const QRegularExpression spaceBeforePunctRe(QStringLiteral(R"( +([,.;:?!]))"));
    out.replace(spaceBeforePunctRe, QStringLiteral("\\1"));
    static const QRegularExpression multiCommaRe(QStringLiteral(R"(,{2,})"));
    out.replace(multiCommaRe, QStringLiteral(","));

    out = out.trimmed();
    if (!out.isEmpty()) {
        out[0] = out[0].toUpper();
    }

    return out;
}




void VoiceEngine::speakText(const QString &text)
{
    stopSpeaking();
    const QString clean = stripMarkdownForSpeech(text);
    if (!clean.isEmpty()) {
        enqueueSentenceForSpeech(clean);
    }
}




void VoiceEngine::stopSpeaking()
{
    m_ttsQueue.clear();
    m_ttsSpeechBuffer.clear();
    m_insideThinkingBlock = false;
    m_bargeInVoiceFrames = 0;

    if (m_currentTtsReply) {
        m_currentTtsReply->abort();
        m_currentTtsReply->deleteLater();
        m_currentTtsReply = nullptr;
    }

    if (m_pwPlayProcess) {
        if (m_pwPlayProcess->state() == QProcess::Running) {
            m_pwPlayProcess->kill();
        }
        m_pwPlayProcess->deleteLater();
        m_pwPlayProcess = nullptr;
    }

    if (m_ttsProcess) {
        if (m_ttsProcess->state() == QProcess::Running) {
            m_ttsProcess->kill();
        }
        m_ttsProcess->deleteLater();
        m_ttsProcess = nullptr;
    }

    m_isSpeakingSentence = false;
    Q_EMIT speakingStateChanged(false);
    if (isSpeaking()) {
        m_isRecording = false;
    Q_EMIT recordingStateChanged(false);
    }
}




void VoiceEngine::processStreamingTtsChunk(const QString &newChunk)
{
    const QString speechMode = resolveSpeechOutputMode();
    if (speechMode == QLatin1String("manual")) {
        return;
    }
    if (speechMode == QLatin1String("voice_only") && !m_lastQueryWasVoice) {
        return;
    }

    // Filter <think> blocks in reasoning models
    if (newChunk.contains(QLatin1String("<think>"))) {
        m_insideThinkingBlock = true;
    }
    m_ttsSpeechBuffer.append(newChunk);
    if (m_insideThinkingBlock) {
        const int closeIdx = m_ttsSpeechBuffer.indexOf(QLatin1String("</think>"));
        if (closeIdx != -1) {
            m_ttsSpeechBuffer.remove(0, closeIdx + 8);
            m_insideThinkingBlock = false;
        } else {
            // Buffer is still inside think tag, don't synthesize
            return;
        }
    }

    // Regex 1: Full sentence terminators (. ? ! or newline)
    // Avoid decimal points (e.g. 3.14) or common Spanish abbreviations (ej., etc., p.ej., Dr., Sr.)
    static const QRegularExpression sentenceTerminator(
        QStringLiteral("(?:[?!\\n]+|(?<!\\b(?:ej|etc|p\\.ej|sr|sra|dr|dra|vs))(?<!\\d)[.](?!\\d)(?:\\s+|$))"),
        QRegularExpression::CaseInsensitiveOption
    );

    // Regex 2: Clause pause boundaries (, ; : —) for sub-sentence low latency streaming
    // Avoid decimals (1,5) or clock format (14:30)
    static const QRegularExpression clauseTerminator(
        QStringLiteral("(?<!\\d)[,;:—]+(?!\\d)(?:\\s+|$)")
    );

    bool extracted = true;
    while (extracted) {
        extracted = false;

        // Check if there is a full sentence terminator
        QRegularExpressionMatch sentMatch = sentenceTerminator.match(m_ttsSpeechBuffer);
        if (sentMatch.hasMatch()) {
            const int endPos = sentMatch.capturedEnd();
            const QString sentence = m_ttsSpeechBuffer.left(endPos).trimmed();
            m_ttsSpeechBuffer = m_ttsSpeechBuffer.mid(endPos);

            const QString clean = stripMarkdownForSpeech(sentence);
            if (!clean.isEmpty()) {
                enqueueSentenceForSpeech(clean);
            }
            extracted = true;
            continue;
        }

        // If no full sentence yet, check if we have an early clause boundary with sufficient length
        // This achieves ultra-low Time-To-First-Speech (< 350ms) like Gemini Live / Siri!
        if (m_ttsSpeechBuffer.length() >= 35) {
            QRegularExpressionMatch clauseMatch = clauseTerminator.match(m_ttsSpeechBuffer);
            if (clauseMatch.hasMatch()) {
                const int endPos = clauseMatch.capturedEnd();
                if (endPos >= 25) {
                    const QString clause = m_ttsSpeechBuffer.left(endPos).trimmed();
                    m_ttsSpeechBuffer = m_ttsSpeechBuffer.mid(endPos);

                    const QString clean = stripMarkdownForSpeech(clause);
                    if (!clean.isEmpty()) {
                        enqueueSentenceForSpeech(clean);
                    }
                    extracted = true;
                }
            }
        }
    }
}




void VoiceEngine::enqueueSentenceForSpeech(const QString &sentence)
{
    m_ttsQueue.enqueue(sentence);
    if (!m_isSpeakingSentence) {
        playNextTtsQueueItem();
    }
}

void VoiceEngine::transcribePcm(const QByteArray &pcmData)
{
    runSpeechTranscription(pcmData);
}

void VoiceEngine::transcribeWavFile(const QString &wavPath)
{
    QFile file(wavPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QByteArray audioData = file.readAll();
    file.close();
    QFile::remove(wavPath);
    const QByteArray pcm = audioData.size() > 44 ? audioData.mid(44) : audioData;
    if (pcm.isEmpty()) {
        return;
    }
    transcribePcm(pcm);
}

void VoiceEngine::injectDictationPcm(const QByteArray &pcmData)
{
    if (m_kdictationClient) {
        m_kdictationClient->transcribeAndInjectPcmAsync(pcmData, m_config.whisperLanguage);
    }
}

void VoiceEngine::injectDictationText(const QString &text)
{
    if (m_kdictationClient) {
        m_kdictationClient->injectText(text.trimmed());
    }
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




void VoiceEngine::playNextTtsQueueItem()
{
    if (m_ttsQueue.isEmpty()) {
        m_isSpeakingSentence = false;
        Q_EMIT speakingStateChanged(false);

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
        return;
    }

    const QString sentence = m_ttsQueue.dequeue();
    speakSentence(sentence);
}




void VoiceEngine::speakSentence(const QString &sentence)
{
    const QString pwPlay = QStandardPaths::findExecutable(QStringLiteral("pw-play"));
    if (pwPlay.isEmpty() || sentence.trimmed().isEmpty()) {
        m_isSpeakingSentence = false;
        playNextTtsQueueItem();
        return;
    }

    const QString ttsEngine = readConfigString(QStringLiteral("ttsEngine"), QStringLiteral("google")).toLower();
    const QString googleApiKey = readConfigString(QStringLiteral("googleApiKey")).trimmed();
    const QString edgeCli = QStandardPaths::findExecutable(QStringLiteral("kity-edge-tts"));
    const QString kokoroCli = QStandardPaths::findExecutable(QStringLiteral("kity-kokoro-tts"));
    const QString piperCli = QStandardPaths::findExecutable(QStringLiteral("piper"));
    const QString piperVoicePath = resolvePiperVoicePath();

    // Golden Gate Voice Customization parameters (Accent, 7 Voice Orbs, Pace, Expressivity)
    const QString voiceAccent = readConfigString(QStringLiteral("voiceAccent"), QStringLiteral("Spanish (Spain)"));
    const int voiceIndex = qBound(0, readConfigInt(QStringLiteral("voiceIndex"), 3), 6);
    const double voicePace = qBound(0.0, readConfigDouble(QStringLiteral("voicePace"), 0.55), 1.0);
    const double voiceExpressivity = qBound(0.0, readConfigDouble(QStringLiteral("voiceExpressivity"), 0.65), 1.0);

    auto resolveEdgeVoiceForOrb = [](const QString &accent, int idx, int &orbPitchShift) -> QString {
        orbPitchShift = 0;
        if (accent.contains(QLatin1String("Latin"), Qt::CaseInsensitive)) {
            static const QStringList voices = {
                QStringLiteral("es-MX-DaliaNeural"), QStringLiteral("es-MX-JorgeNeural"),
                QStringLiteral("es-CO-SalomeNeural"), QStringLiteral("es-AR-ElenaNeural"),
                QStringLiteral("es-CL-CatalinaNeural"), QStringLiteral("es-US-PalomaNeural"),
                QStringLiteral("es-PE-CamilaNeural")
            };
            return voices.value(idx, voices.at(0));
        }
        if (accent.contains(QLatin1String("American"), Qt::CaseInsensitive)) {
            static const QStringList voices = {
                QStringLiteral("en-US-AndrewNeural"), QStringLiteral("en-US-BrianNeural"),
                QStringLiteral("en-US-GuyNeural"), QStringLiteral("en-US-AvaNeural"),
                QStringLiteral("en-US-EmmaNeural"), QStringLiteral("en-US-JennyNeural"),
                QStringLiteral("en-US-AriaNeural")
            };
            return voices.value(idx, voices.at(3));
        }
        if (accent.contains(QLatin1String("British"), Qt::CaseInsensitive)) {
            static const QStringList voices = {
                QStringLiteral("en-GB-RyanNeural"), QStringLiteral("en-GB-ThomasNeural"),
                QStringLiteral("en-GB-RyanNeural"), QStringLiteral("en-GB-SoniaNeural"),
                QStringLiteral("en-GB-MaisieNeural"), QStringLiteral("en-GB-LibbyNeural"),
                QStringLiteral("en-GB-LibbyNeural")
            };
            static const int shifts[] = { -3, 0, +4, 0, 0, 0, +4 };
            orbPitchShift = shifts[qBound(0, idx, 6)];
            return voices.value(idx, voices.at(3));
        }
        // Spanish (Spain): Edge has 3 native actors (Alvaro, Elvira, Ximena).
        // Modulated into 7 distinct acoustic personas via harmonic pitch offsets.
        static const QStringList esVoices = {
            QStringLiteral("es-ES-AlvaroNeural"), QStringLiteral("es-ES-AlvaroNeural"),
            QStringLiteral("es-ES-AlvaroNeural"), QStringLiteral("es-ES-ElviraNeural"),
            QStringLiteral("es-ES-ElviraNeural"), QStringLiteral("es-ES-XimenaNeural"),
            QStringLiteral("es-ES-XimenaNeural")
        };
        static const int esShifts[] = { -6, 0, +5, 0, +4, -2, +4 };
        orbPitchShift = esShifts[qBound(0, idx, 6)];
        return esVoices.value(idx, esVoices.at(3));
    };

    auto resolveGoogleVoiceForOrb = [](const QString &accent, int idx, QString &langCodeOut) -> QString {
        if (accent.contains(QLatin1String("Latin"), Qt::CaseInsensitive)) {
            langCodeOut = QStringLiteral("es-US");
            static const QStringList voices = {
                QStringLiteral("es-US-Neural2-A"), QStringLiteral("es-US-Neural2-B"),
                QStringLiteral("es-US-Neural2-C"), QStringLiteral("es-US-Neural2-A"),
                QStringLiteral("es-US-Studio-B"), QStringLiteral("es-US-Wavenet-A"),
                QStringLiteral("es-US-Wavenet-B")
            };
            return voices.value(idx, voices.at(3));
        }
        if (accent.contains(QLatin1String("American"), Qt::CaseInsensitive)) {
            langCodeOut = QStringLiteral("en-US");
            static const QStringList voices = {
                QStringLiteral("en-US-Neural2-C"), QStringLiteral("en-US-Neural2-D"),
                QStringLiteral("en-US-Neural2-E"), QStringLiteral("en-US-Neural2-F"),
                QStringLiteral("en-US-Neural2-G"), QStringLiteral("en-US-Neural2-H"),
                QStringLiteral("en-US-Neural2-I")
            };
            return voices.value(idx, voices.at(3));
        }
        if (accent.contains(QLatin1String("British"), Qt::CaseInsensitive)) {
            langCodeOut = QStringLiteral("en-GB");
            static const QStringList voices = {
                QStringLiteral("en-GB-Neural2-A"), QStringLiteral("en-GB-Neural2-B"),
                QStringLiteral("en-GB-Neural2-C"), QStringLiteral("en-GB-Neural2-D"),
                QStringLiteral("en-GB-Neural2-F"), QStringLiteral("en-GB-Wavenet-A"),
                QStringLiteral("en-GB-Wavenet-B")
            };
            return voices.value(idx, voices.at(3));
        }
        langCodeOut = QStringLiteral("es-ES");
        static const QStringList esVoices = {
            QStringLiteral("es-ES-Neural2-B"), QStringLiteral("es-ES-Neural2-A"),
            QStringLiteral("es-ES-Neural2-C"), QStringLiteral("es-ES-Neural2-F"),
            QStringLiteral("es-ES-Neural2-D"), QStringLiteral("es-ES-Neural2-E"),
            QStringLiteral("es-ES-Neural2-F")
        };
        return esVoices.value(idx, esVoices.at(3));
    };

    // 1. Google Cloud Text-to-Speech API (Neural2 / Journey / Studio voices)
    if (ttsEngine == QLatin1String("google") && !googleApiKey.isEmpty()) {
        m_isSpeakingSentence = true;
    Q_EMIT speakingStateChanged(true);
        // speaking

        if (m_wakeWordListening && (!m_wakeWordRecorder || !m_wakeWordRecorder->isRecording())) {
            startWakeWordListener();
        }

        QString langCode = QStringLiteral("es-ES");
        const QString googleVoice = resolveGoogleVoiceForOrb(voiceAccent, voiceIndex, langCode);
        QJsonObject inputObj;
        inputObj[QStringLiteral("text")] = sentence;

        QJsonObject voiceObj;
        voiceObj[QStringLiteral("languageCode")] = langCode;
        voiceObj[QStringLiteral("name")] = googleVoice;

        QJsonObject audioConfig;
        audioConfig[QStringLiteral("audioEncoding")] = QStringLiteral("MP3");
        audioConfig[QStringLiteral("speakingRate")] = 0.65 + (voicePace * 0.75);
        audioConfig[QStringLiteral("pitch")] = (voiceExpressivity - 0.5) * 10.0;

        QJsonObject payload;
        payload[QStringLiteral("input")] = inputObj;
        payload[QStringLiteral("voice")] = voiceObj;
        payload[QStringLiteral("audioConfig")] = audioConfig;

        const QUrl gUrl(QStringLiteral("https://texttospeech.googleapis.com/v1/text:synthesize?key=%1").arg(googleApiKey));
        QNetworkRequest req(gUrl);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

        m_currentTtsReply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        connect(m_currentTtsReply.data(), &QNetworkReply::finished, this, [this, pwPlay, edgeCli, resolveEdgeVoiceForOrb, voiceAccent, voiceIndex, voicePace, voiceExpressivity, sentence]() {
            if (!m_currentTtsReply) {
                return;
            }
            QNetworkReply *gReply = m_currentTtsReply;
            m_currentTtsReply = nullptr;
            gReply->deleteLater();

            if (!m_isSpeakingSentence) {
                return;
            }

            if (gReply->error() != QNetworkReply::NoError) {
                qWarning() << "Kity: Google Cloud TTS error:" << gReply->errorString() << "- Falling back to Edge-TTS";
                if (!edgeCli.isEmpty()) {
                    int orbPitchShift = 0;
                    const QString edgeVoice = resolveEdgeVoiceForOrb(voiceAccent, voiceIndex, orbPitchShift);
                    const int ratePct = qRound((voicePace - 0.5) * 80.0);
                    const int pitchHz = qRound((voiceExpressivity - 0.5) * 28.0);
                    const int totalPitchHz = pitchHz + orbPitchShift;
                    const QString rateArg = QStringLiteral("--rate=%1%2%").arg(ratePct >= 0 ? QStringLiteral("+") : QString()).arg(ratePct);
                    const QString pitchArg = QStringLiteral("--pitch=%1%2Hz").arg(totalPitchHz >= 0 ? QStringLiteral("+") : QString()).arg(totalPitchHz);

                    m_ttsProcess = new QProcess(this);
                    m_pwPlayProcess = new QProcess(this);
                    m_ttsProcess->setStandardOutputProcess(m_pwPlayProcess.data());

                    connect(m_pwPlayProcess.data(), &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
                        if (m_pwPlayProcess) {
                            m_pwPlayProcess->deleteLater();
                            m_pwPlayProcess = nullptr;
                        }
                        if (m_ttsProcess) {
                            m_ttsProcess->deleteLater();
                            m_ttsProcess = nullptr;
                        }
                        playNextTtsQueueItem();
                    });

                    m_ttsProcess->start(edgeCli, {QStringLiteral("--voice"), edgeVoice, rateArg, pitchArg, QStringLiteral("--text"), sentence, QStringLiteral("--write-media"), QStringLiteral("-")});
                    m_pwPlayProcess->start(pwPlay, {QStringLiteral("-")});
                    return;
                }
                playNextTtsQueueItem();
                return;
            }

            const QJsonDocument doc = QJsonDocument::fromJson(gReply->readAll());
            const QString b64 = doc.object().value(QStringLiteral("audioContent")).toString();
            if (b64.isEmpty()) {
                playNextTtsQueueItem();
                return;
            }

            const QByteArray audioBytes = QByteArray::fromBase64(b64.toUtf8());
            m_pwPlayProcess = new QProcess(this);
            connect(m_pwPlayProcess.data(), &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
                if (m_pwPlayProcess) {
                    m_pwPlayProcess->deleteLater();
                    m_pwPlayProcess = nullptr;
                }
                playNextTtsQueueItem();
            });

            m_pwPlayProcess->start(pwPlay, QStringList() << QStringLiteral("-"));
            m_pwPlayProcess->write(audioBytes);
            m_pwPlayProcess->closeWriteChannel();
        });
        return;
    }

    // 2. Microsoft Edge Neural TTS (Free high-definition Castilian Spanish: Elvira / Ximena)
    // Used when engine is 'edge' OR when 'google' is selected but no API key is entered yet
    if ((ttsEngine == QLatin1String("edge") || (ttsEngine == QLatin1String("google") && googleApiKey.isEmpty())) && !edgeCli.isEmpty()) {
        m_isSpeakingSentence = true;
    Q_EMIT speakingStateChanged(true);
        // speaking

        if (m_wakeWordListening && (!m_wakeWordRecorder || !m_wakeWordRecorder->isRecording())) {
            startWakeWordListener();
        }

        int orbPitchShift = 0;
        const QString edgeVoice = resolveEdgeVoiceForOrb(voiceAccent, voiceIndex, orbPitchShift);
        const int ratePct = qRound((voicePace - 0.5) * 80.0);
        const int pitchHz = qRound((voiceExpressivity - 0.5) * 28.0);
        const int totalPitchHz = pitchHz + orbPitchShift;
        const QString rateArg = QStringLiteral("--rate=%1%2%").arg(ratePct >= 0 ? QStringLiteral("+") : QString()).arg(ratePct);
        const QString pitchArg = QStringLiteral("--pitch=%1%2Hz").arg(totalPitchHz >= 0 ? QStringLiteral("+") : QString()).arg(totalPitchHz);

        m_ttsProcess = new QProcess(this);
        m_pwPlayProcess = new QProcess(this);

        m_ttsProcess->setStandardOutputProcess(m_pwPlayProcess.data());

        QStringList edgeArgs;
        edgeArgs << QStringLiteral("--voice") << edgeVoice
                 << rateArg
                 << pitchArg
                 << QStringLiteral("--text") << sentence
                 << QStringLiteral("--write-media") << QStringLiteral("-");

        connect(m_pwPlayProcess.data(), &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
            if (m_pwPlayProcess) {
                m_pwPlayProcess->deleteLater();
                m_pwPlayProcess = nullptr;
            }
            if (m_ttsProcess) {
                m_ttsProcess->deleteLater();
                m_ttsProcess = nullptr;
            }
            playNextTtsQueueItem();
        });

        m_ttsProcess->start(edgeCli, edgeArgs);
        m_pwPlayProcess->start(pwPlay, QStringList() << QStringLiteral("-"));
        return;
    }

    // 3. Local TTS engines: Kokoro-82M or Piper
    QString execProg;
    QStringList execArgs;

    if (ttsEngine == QLatin1String("kokoro") && !kokoroCli.isEmpty()) {
        execProg = kokoroCli;
        const QString kokoroVoice = readConfigString(QStringLiteral("kokoroVoice"), QStringLiteral("ef_dora"));
        execArgs << QStringLiteral("--voice") << kokoroVoice
                 << QStringLiteral("--output_file") << QStringLiteral("-");
    } else if (!piperCli.isEmpty() && !piperVoicePath.isEmpty()) {
        execProg = piperCli;
        const double lengthScale = 1.35 - (voicePace * 0.65);
        const double noiseScale = 0.35 + (voiceExpressivity * 0.50);
        execArgs << QStringLiteral("--model") << piperVoicePath
                 << QStringLiteral("--length_scale") << QString::number(lengthScale, 'f', 2)
                 << QStringLiteral("--noise_scale") << QString::number(noiseScale, 'f', 2)
                 << QStringLiteral("--output_file") << QStringLiteral("-");
    } else if (!kokoroCli.isEmpty()) {
        execProg = kokoroCli;
        execArgs << QStringLiteral("--voice") << QStringLiteral("ef_dora")
                 << QStringLiteral("--output_file") << QStringLiteral("-");
    } else {
        m_isSpeakingSentence = false;
        Q_EMIT speakingStateChanged(false);
        playNextTtsQueueItem();
        return;
    }

    m_isSpeakingSentence = true;
    Q_EMIT speakingStateChanged(true);
    // speaking

    if (m_wakeWordListening && (!m_wakeWordRecorder || !m_wakeWordRecorder->isRecording())) {
        startWakeWordListener();
    }

    m_ttsProcess = new QProcess(this);
    m_pwPlayProcess = new QProcess(this);

    m_ttsProcess->setStandardOutputProcess(m_pwPlayProcess.data());

    connect(m_pwPlayProcess.data(), &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        if (m_pwPlayProcess) {
            m_pwPlayProcess->deleteLater();
            m_pwPlayProcess = nullptr;
        }
        if (m_ttsProcess) {
            m_ttsProcess->deleteLater();
            m_ttsProcess = nullptr;
        }
        playNextTtsQueueItem();
    });

    m_ttsProcess->start(execProg, execArgs);
    m_pwPlayProcess->start(pwPlay, QStringList() << QStringLiteral("-"));

    m_ttsProcess->write(sentence.toUtf8());
    m_ttsProcess->closeWriteChannel();
}




QString VoiceEngine::stripMarkdownForSpeech(const QString &markdown)
{
    QString res = markdown;

    // Remove thinking tags if any leaked
    static const QRegularExpression thinkRegex(QStringLiteral("<think>[\\s\\S]*?</think>|<think>[\\s\\S]*$"));
    res.remove(thinkRegex);

    // Remove code blocks and inline backticks
    static const QRegularExpression codeRegex(QStringLiteral("```[\\s\\S]*?```|`.*?`"));
    res.remove(codeRegex);

    // Remove links [text](url) -> text
    static const QRegularExpression linkRegex(QStringLiteral("\\[([^\\]]+)\\]\\([^\\)]+\\)"));
    res.replace(linkRegex, QStringLiteral("\\1"));

    // Remove HTML tags
    static const QRegularExpression htmlRegex(QStringLiteral("<[^>]+>"));
    res.remove(htmlRegex);

    // Remove leading bullet list dashes/asterisks (e.g. "- item" or "* item" -> "item")
    static const QRegularExpression listRegex(QStringLiteral("^[\\s]*[-*•]\\s+"), QRegularExpression::MultilineOption);
    res.remove(listRegex);

    // Phonetic normalization for natural speech verbalization (Siri style):
    // 1. Temperatures: 15ºC, 15°C, 15º, 15° -> "15 grados centígrados" / "15 grados"
    static const QRegularExpression tempCRegex(QStringLiteral("(\\d+(?:[.,]\\d+)?)\\s*(?:º|°)\\s*C\\b"), QRegularExpression::CaseInsensitiveOption);
    res.replace(tempCRegex, QStringLiteral("\\1 grados centígrados"));

    static const QRegularExpression tempFRegex(QStringLiteral("(\\d+(?:[.,]\\d+)?)\\s*(?:º|°)\\s*F\\b"), QRegularExpression::CaseInsensitiveOption);
    res.replace(tempFRegex, QStringLiteral("\\1 grados Fahrenheit"));

    static const QRegularExpression degRegex(QStringLiteral("(\\d+(?:[.,]\\d+)?)\\s*(?:º|°)(?!\\w)"));
    res.replace(degRegex, QStringLiteral("\\1 grados"));

    // 2. Volume & Capacity: ml, cl, l
    static const QRegularExpression mlRegex(QStringLiteral("(\\d+(?:[.,]\\d+)?)\\s*ml\\b"), QRegularExpression::CaseInsensitiveOption);
    res.replace(mlRegex, QStringLiteral("\\1 mililitros"));

    static const QRegularExpression clRegex(QStringLiteral("(\\d+(?:[.,]\\d+)?)\\s*cl\\b"), QRegularExpression::CaseInsensitiveOption);
    res.replace(clRegex, QStringLiteral("\\1 centilitros"));

    // 3. Weight & Mass: kg, g, gr, mg
    static const QRegularExpression kgRegex(QStringLiteral("(\\d+(?:[.,]\\d+)?)\\s*kg\\b"), QRegularExpression::CaseInsensitiveOption);
    res.replace(kgRegex, QStringLiteral("\\1 kilos"));

    static const QRegularExpression grRegex(QStringLiteral("(\\d+(?:[.,]\\d+)?)\\s*(?:gr\\.?|g)\\b"), QRegularExpression::CaseInsensitiveOption);
    res.replace(grRegex, QStringLiteral("\\1 gramos"));

    static const QRegularExpression mgRegex(QStringLiteral("(\\d+(?:[.,]\\d+)?)\\s*mg\\b"), QRegularExpression::CaseInsensitiveOption);
    res.replace(mgRegex, QStringLiteral("\\1 miligramos"));

    // 4. Speed & Distance: km/h, km, m/s, m, cm, mm
    static const QRegularExpression kmhRegex(QStringLiteral("(\\d+(?:[.,]\\d+)?)\\s*km/h\\b"), QRegularExpression::CaseInsensitiveOption);
    res.replace(kmhRegex, QStringLiteral("\\1 kilómetros por hora"));

    static const QRegularExpression kmRegex(QStringLiteral("(\\d+(?:[.,]\\d+)?)\\s*km\\b"), QRegularExpression::CaseInsensitiveOption);
    res.replace(kmRegex, QStringLiteral("\\1 kilómetros"));

    static const QRegularExpression cmRegex(QStringLiteral("(\\d+(?:[.,]\\d+)?)\\s*cm\\b"), QRegularExpression::CaseInsensitiveOption);
    res.replace(cmRegex, QStringLiteral("\\1 centímetros"));

    static const QRegularExpression mmRegex(QStringLiteral("(\\d+(?:[.,]\\d+)?)\\s*mm\\b"), QRegularExpression::CaseInsensitiveOption);
    res.replace(mmRegex, QStringLiteral("\\1 milímetros"));

    // 5. Percentages & Currencies: %, €, $
    static const QRegularExpression pctRegex(QStringLiteral("(\\d+(?:[.,]\\d+)?)\\s*%"));
    res.replace(pctRegex, QStringLiteral("\\1 por ciento"));

    static const QRegularExpression eurPostRegex(QStringLiteral("(\\d+(?:[.,]\\d+)?)\\s*€"));
    res.replace(eurPostRegex, QStringLiteral("\\1 euros"));

    static const QRegularExpression eurPreRegex(QStringLiteral("€\\s*(\\d+(?:[.,]\\d+)?)"));
    res.replace(eurPreRegex, QStringLiteral("\\1 euros"));

    // Remove markdown formatting symbols (*, #, _, >, ~, $, \)
    static const QRegularExpression symRegex(QStringLiteral("[*#_>~$\\\\]"));
    res.remove(symRegex);

    // Remove emojis to prevent TTS pronouncing technical Unicode names
    static const QRegularExpression emojiRegex(
        QStringLiteral("[\\x{1F300}-\\x{1FAD6}\\x{1F900}-\\x{1F9FF}\\x{2600}-\\x{26FF}\\x{2700}-\\x{27BF}]"),
        QRegularExpression::UseUnicodePropertiesOption
    );
    res.remove(emojiRegex);

    // Collapse multiple spaces/newlines into a single clean space
    static const QRegularExpression multiSpaceRegex(QStringLiteral("\\s+"));
    res = res.replace(multiSpaceRegex, QStringLiteral(" "));

    return res.trimmed();
}




void VoiceEngine::startWakeWordListener()
{
    if (!m_wakeWordListening) {
        return;
    }
    m_wakeWordListening = true;

    if (m_isRecording) {
        return;
    }
    m_wakeWordListening = true;

    if (m_wakeWordRecorder && m_wakeWordRecorder->isRecording()) {
        return;
    }
    m_wakeWordListening = true;

    bool isBtOnly = false;
    const QString targetSource = resolvePreferredCaptureSource(&isBtOnly);
    if (targetSource.isEmpty()) {
        qInfo() << "Kity: Skipping background wake word listener because no active non-Bluetooth capture source is available (bluetoothPresent="
                << isBtOnly << ")";
        return;
    }
    m_wakeWordListening = true;

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

    // Live Full-Duplex Barge-in detection: if Kity is speaking and user begins speaking directly
    if (isSpeaking()) {
        const double bargeInThreshold = isPipeWireEchoCancelAvailable() ? 0.026 : 0.045;
        if (rms >= bargeInThreshold) {
            m_bargeInVoiceFrames++;
            if (m_bargeInVoiceFrames >= 2) { // 2 consecutive chunks (~160ms) of active user speech
                qInfo() << "Kity: Live Full-Duplex Barge-in triggered! User spoke while Kity was speaking. Aborting speech.";
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

    // Voice detected above background noise (>= 0.016) and not in cooldown
    if (rms >= 0.016 && (!m_wakeWordCooldown.isValid() || m_wakeWordCooldown.elapsed() > 1200)) {
        if (m_wakeWordPcmBuffer.size() >= 24000) { // At least ~0.75s buffered
            m_wakeWordEvaluating = true;
            qInfo() << "Kity: Speech detected in background stream (RMS:" << rms << "), capturing wake word window...";

            // Allow 650ms for user to finish saying "Hola Kity"
            QTimer::singleShot(650, this, [this]() {
                if (m_isRecording) {
                    m_wakeWordEvaluating = false;
                    return;
                }
                const QByteArray pcmData = m_wakeWordPcmBuffer.right(64000); // last ~2 seconds
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

    // Inferencia rápida memoria-a-memoria en RAM con Warm Model (<60ms)
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
    if (!readConfigBool(QStringLiteral("earconsEnabled"), true)) {
        return;
    }

    const QString pwPlay = QStandardPaths::findExecutable(QStringLiteral("pw-play"));
    if (pwPlay.isEmpty()) {
        return;
    }

    const QString fileName = activating ? QStringLiteral("kity_on.wav") : QStringLiteral("kity_off.wav");
    QString earconPath = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("kity/sounds/") + fileName);
    if (earconPath.isEmpty()) {
        earconPath = QDir::homePath() + QStringLiteral("/.local/share/kity/sounds/") + fileName;
    }

    if (!QFileInfo::exists(earconPath)) {
        earconPath = QDir::tempPath() + (activating ? QStringLiteral("/kity_earcon_on.wav") : QStringLiteral("/kity_earcon_off.wav"));
        if (!QFileInfo::exists(earconPath)) {
            // Synthesize high-fidelity 44.1kHz crystalline dual-tone harmonic Siri earcon
            const int sampleRate = 44100;
            const double durationSec = 0.16;
            const int totalSamples = static_cast<int>(sampleRate * durationSec);
            QByteArray pcm(totalSamples * static_cast<int>(sizeof(qint16)), 0);
            qint16 *samples = reinterpret_cast<qint16 *>(pcm.data());

            const double f1 = activating ? 987.77 : 1318.51; // B5 -> E6 ascending, or E6 -> B5 descending
            const double f2 = activating ? 1318.51 : 987.77;
            const double tSwitch = 0.055;
            const double twoPi = 6.283185307179586;

            for (int i = 0; i < totalSamples; ++i) {
                const double t = static_cast<double>(i) / sampleRate;
                const double env1 = (t < 0.09) ? std::exp(-t / 0.035) : 0.0;
                double env2 = 0.0;
                if (t >= tSwitch) {
                    const double t2 = t - tSwitch;
                    const double attack = qMin(1.0, t2 / 0.005);
                    env2 = attack * std::exp(-t2 / 0.055);
                }

                const double tone1 = std::sin(twoPi * f1 * t) + 0.25 * std::sin(4.0 * M_PI * f1 * t);
                const double t2 = qMax(0.0, t - tSwitch);
                const double tone2 = std::sin(twoPi * f2 * t2) + 0.20 * std::sin(4.0 * M_PI * f2 * t2);

                double val = (tone1 * env1 * 0.18) + (tone2 * env2 * 0.22);
                val = qBound(-1.0, val, 1.0);
                samples[i] = static_cast<qint16>(val * 32767.0);
            }

            QFile f(earconPath);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(createWavHeader(pcm.size(), sampleRate, 1, 16));
                f.write(pcm);
                f.close();
            }
        }
    }

    if (QFileInfo::exists(earconPath)) {
        QProcess::startDetached(pwPlay, {earconPath});
    }
}




QByteArray VoiceEngine::createWavHeader(int dataSize, int sampleRate, int channels, int bitsPerSample)
{
    QByteArray header(44, 0);
    char *h = header.data();

    // RIFF header
    memcpy(h, "RIFF", 4);
    const qint32 chunkSize = 36 + dataSize;
    memcpy(h + 4, &chunkSize, 4);
    memcpy(h + 8, "WAVE", 4);

    // fmt subchunk
    memcpy(h + 12, "fmt ", 4);
    const qint32 subchunk1Size = 16;
    memcpy(h + 16, &subchunk1Size, 4);
    const qint16 audioFormat = 1; // PCM
    memcpy(h + 20, &audioFormat, 2);
    const qint16 numChannels = static_cast<qint16>(channels);
    memcpy(h + 22, &numChannels, 2);
    const qint32 sRate = sampleRate;
    memcpy(h + 24, &sRate, 4);
    const qint32 byteRate = sampleRate * channels * (bitsPerSample / 8);
    memcpy(h + 28, &byteRate, 4);
    const qint16 blockAlign = static_cast<qint16>(channels * (bitsPerSample / 8));
    memcpy(h + 32, &blockAlign, 2);
    const qint16 bitsPerSamp = static_cast<qint16>(bitsPerSample);
    memcpy(h + 34, &bitsPerSamp, 2);

    // data subchunk
    memcpy(h + 36, "data", 4);
    const qint32 dSize = dataSize;
    memcpy(h + 40, &dSize, 4);

    return header;
}


} // namespace Kity
