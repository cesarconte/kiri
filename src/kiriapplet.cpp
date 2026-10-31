/*
    SPDX-FileCopyrightText: 2026 Kiri contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "kiriapplet.h"
#include "debug.h"

#include <cmath>

#include <KConfigGroup>
#include <KLocalizedString>
#include <KNotification>
#include <KPluginFactory>
#include <KService>

#include <QBuffer>
#include <QClipboard>
#include <QCoreApplication>
#include <QCursor>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QOperatingSystemVersion>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QUrlQuery>

K_PLUGIN_CLASS_WITH_JSON(KiriApplet, "metadata.json")

KiriApplet::KiriApplet(QObject *parent, const KPluginMetaData &data, const QVariantList &args)
    : Plasma::Applet(parent, data, args)
    , m_nam(new QNetworkAccessManager(this))
    , m_vadTimer(new QTimer(this))
    , m_reminderTimer(new QTimer(this))
    , m_readinessTimer(new QTimer(this))
{
}

KiriApplet::~KiriApplet()
{
    stopRecording();
    stopSpeaking();
    stopWakeWordListener();
}

void KiriApplet::init()
{
    Plasma::Applet::init();

    loadConversationHistory();
    loadContacts();
    loadScheduledReminders();
    ensureKnowledgeBaseReady();

    connect(m_vadTimer, &QTimer::timeout, this, &KiriApplet::checkVoiceActivity);
    connect(m_reminderTimer, &QTimer::timeout, this, &KiriApplet::checkScheduledReminders);
    m_reminderTimer->start(30000); // Check reminders every 30s

    connect(m_readinessTimer, &QTimer::timeout, this, &KiriApplet::checkReadiness);
    m_readinessTimer->start(60000); // Check service health every minute

    checkReadiness();
    refreshModels();

    m_wakeWordEnabled = readConfigBool(QStringLiteral("wakeWordEnabled"), false);
    if (m_wakeWordEnabled) {
        startWakeWordListener();
    }
}

void KiriApplet::configChanged()
{
    Plasma::Applet::configChanged();

    // 1. Sync wake word listener state with updated configuration
    const bool wakeEnabled = readConfigBool(QStringLiteral("wakeWordEnabled"), false);
    setWakeWordEnabled(wakeEnabled);

    // 2. Re-evaluate models and audio stack readiness
    checkReadiness();
}

// =========================================================================
// Property Accessors
// =========================================================================

QString KiriApplet::state() const { return m_state; }
QString KiriApplet::statusMessage() const { return m_statusMessage; }
QString KiriApplet::streamingText() const { return m_streamingText; }
QVariantList KiriApplet::messages() const { return m_messages; }
QStringList KiriApplet::availableModels() const { return m_availableModels; }
bool KiriApplet::ollamaReachable() const { return m_ollamaReachable; }
bool KiriApplet::whisperAvailable() const { return m_whisperAvailable; }
bool KiriApplet::piperAvailable() const { return m_piperAvailable; }
bool KiriApplet::isRecording() const { return m_state == QLatin1String("listening"); }
bool KiriApplet::isBusy() const { return m_state == QLatin1String("thinking") || m_state == QLatin1String("transcribing"); }
bool KiriApplet::isSpeaking() const { return m_state == QLatin1String("speaking") || m_isSpeakingSentence; }
qreal KiriApplet::audioLevel() const { return m_audioLevel; }
bool KiriApplet::wakeWordEnabled() const { return m_wakeWordEnabled; }

void KiriApplet::setWakeWordEnabled(bool enabled)
{
    if (m_wakeWordEnabled != enabled) {
        m_wakeWordEnabled = enabled;
        Q_EMIT wakeWordEnabledChanged();
        if (m_wakeWordEnabled) {
            startWakeWordListener();
        } else {
            stopWakeWordListener();
        }
    }
}

void KiriApplet::setState(const QString &newState, const QString &newStatus)
{
    const bool stateChangedFlag = (m_state != newState);
    const bool statusChangedFlag = (m_statusMessage != newStatus);

    m_state = newState;
    if (!newStatus.isNull()) {
        m_statusMessage = newStatus;
    }

    if (stateChangedFlag) {
        Q_EMIT stateChanged();
    }
    if (statusChangedFlag) {
        Q_EMIT statusMessageChanged();
    }

    if (newState == QLatin1String("idle") && m_wakeWordEnabled) {
        QTimer::singleShot(500, this, [this]() {
            if (m_state == QLatin1String("idle") && m_wakeWordEnabled) {
                startWakeWordListener();
            }
        });
    }
}

// =========================================================================
// Configuration Readers
// =========================================================================

QString KiriApplet::readConfigString(const QString &key, const QString &def) const
{
    const KConfigGroup cfg = config();
    if (cfg.hasGroup(QStringLiteral("General")) && cfg.group(QStringLiteral("General")).hasKey(key)) {
        return cfg.group(QStringLiteral("General")).readEntry(key, def);
    }
    return cfg.readEntry(key, def);
}

int KiriApplet::readConfigInt(const QString &key, int def) const
{
    const KConfigGroup cfg = config();
    if (cfg.hasGroup(QStringLiteral("General")) && cfg.group(QStringLiteral("General")).hasKey(key)) {
        return cfg.group(QStringLiteral("General")).readEntry(key, def);
    }
    return cfg.readEntry(key, def);
}

double KiriApplet::readConfigDouble(const QString &key, double def) const
{
    const KConfigGroup cfg = config();
    if (cfg.hasGroup(QStringLiteral("General")) && cfg.group(QStringLiteral("General")).hasKey(key)) {
        return cfg.group(QStringLiteral("General")).readEntry(key, def);
    }
    return cfg.readEntry(key, def);
}

bool KiriApplet::readConfigBool(const QString &key, bool def) const
{
    const KConfigGroup cfg = config();
    if (cfg.hasGroup(QStringLiteral("General")) && cfg.group(QStringLiteral("General")).hasKey(key)) {
        return cfg.group(QStringLiteral("General")).readEntry(key, def);
    }
    return cfg.readEntry(key, def);
}

QString KiriApplet::resolveOllamaUrl() const
{
    QString url = readConfigString(QStringLiteral("ollamaUrl"), QStringLiteral("http://127.0.0.1:11434"));
    while (url.endsWith(QLatin1Char('/'))) {
        url.chop(1);
    }
    return url;
}

QString KiriApplet::resolveOllamaModel() const
{
    return readConfigString(QStringLiteral("ollamaModel"), QStringLiteral("qwen2.5:7b"));
}

QString KiriApplet::resolveVisionModel() const
{
    return readConfigString(QStringLiteral("ollamaVisionModel"), QStringLiteral("moondream"));
}

QString KiriApplet::resolveSystemPrompt() const
{
    return readConfigString(QStringLiteral("systemPrompt"),
        QStringLiteral("Eres Kiri, un asistente de voz e inteligencia artificial nativo para KDE Plasma en CachyOS Linux. "
                       "Responde SIEMPRE en español de España (castellano), de forma concisa, clara, directa y natural, optimizada para síntesis de voz (TTS)."));
}

QString KiriApplet::resolveWhisperModelPath() const
{
    QString path = readConfigString(QStringLiteral("whisperModelPath"));
    if (!path.isEmpty() && QFile::exists(path)) {
        return path;
    }
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kiri/models");
    const QString defaultPath = dataDir + QStringLiteral("/ggml-base.bin");
    if (QFile::exists(defaultPath)) {
        return defaultPath;
    }
    return QString();
}

QString KiriApplet::resolveWhisperLanguage() const
{
    const QString lang = readConfigString(QStringLiteral("whisperLanguage"), QStringLiteral("es")).trimmed().toLower();
    if (lang.isEmpty() || lang == QStringLiteral("auto")) {
        return QStringLiteral("es");
    }
    return lang;
}

QString KiriApplet::resolvePiperVoicePath() const
{
    QString path = readConfigString(QStringLiteral("piperVoicePath"));
    if (!path.isEmpty() && QFile::exists(path)) {
        return path;
    }
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kiri/voices");
    const QString defaultVoice = dataDir + QStringLiteral("/es_ES-sharvard-medium.onnx");
    if (QFile::exists(defaultVoice)) {
        return defaultVoice;
    }
    return QString();
}

QString KiriApplet::resolveSpeechOutputMode() const
{
    return readConfigString(QStringLiteral("speechOutputMode"), QStringLiteral("always"));
}

// =========================================================================
// Health & Readiness
// =========================================================================

void KiriApplet::checkReadiness()
{
    // 1. Whisper check
    const bool hasPwRecord = !QStandardPaths::findExecutable(QStringLiteral("pw-record")).isEmpty();
    const bool hasWhisperCli = !QStandardPaths::findExecutable(QStringLiteral("whisper-cli")).isEmpty() ||
                               !QStandardPaths::findExecutable(QStringLiteral("whisper")).isEmpty();
    const bool hasWhisperModel = !resolveWhisperModelPath().isEmpty();
    const bool newWhisperAvailable = hasPwRecord && hasWhisperCli && hasWhisperModel;

    // 2. Piper check
    const bool hasPiperCli = !QStandardPaths::findExecutable(QStringLiteral("piper")).isEmpty();
    const bool hasPiperVoice = !resolvePiperVoicePath().isEmpty();
    const bool hasPwPlay = !QStandardPaths::findExecutable(QStringLiteral("pw-play")).isEmpty();
    const bool newPiperAvailable = hasPiperCli && hasPiperVoice && hasPwPlay;

    // 3. Ollama check (GET /api/tags)
    const QUrl tagsUrl(resolveOllamaUrl() + QStringLiteral("/api/tags"));
    QNetworkRequest req(tagsUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KiriApplet/1.0"));

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, newWhisperAvailable, newPiperAvailable]() {
        reply->deleteLater();
        const bool reachable = (reply->error() == QNetworkReply::NoError);
        bool modelsChanged = false;

        if (reachable) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            const QJsonArray modelsArr = doc.object().value(QStringLiteral("models")).toArray();
            QStringList models;
            for (const QJsonValue &v : modelsArr) {
                const QString mName = v.toObject().value(QStringLiteral("name")).toString();
                if (!mName.isEmpty()) {
                    models.append(mName);
                }
            }
            if (models != m_availableModels) {
                m_availableModels = models;
                modelsChanged = true;
            }
        }

        const bool readinessChangedFlag = (m_ollamaReachable != reachable) ||
                                          (m_whisperAvailable != newWhisperAvailable) ||
                                          (m_piperAvailable != newPiperAvailable);

        m_ollamaReachable = reachable;
        m_whisperAvailable = newWhisperAvailable;
        m_piperAvailable = newPiperAvailable;

        if (readinessChangedFlag) {
            Q_EMIT readinessChanged();
        }
        if (modelsChanged) {
            Q_EMIT availableModelsChanged();
        }
    });
}

void KiriApplet::refreshModels()
{
    checkReadiness();
}

// =========================================================================
// Conversation & Ollama LLM Interaction
// =========================================================================

void KiriApplet::sendMessage(const QString &text, bool fromVoice)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    m_lastQueryWasVoice = fromVoice;

    // Append user message to history
    QVariantMap userMsg;
    userMsg[QStringLiteral("role")] = QStringLiteral("user");
    userMsg[QStringLiteral("content")] = trimmed;
    userMsg[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    userMsg[QStringLiteral("fromVoice")] = fromVoice;
    m_messages.append(userMsg);
    Q_EMIT messagesChanged();
    saveConversationHistory();

    // 1. Check if input matches an immediate system action
    if (detectAndExecuteSystemAction(trimmed)) {
        return;
    }

    // 2. Fetch context (weather, web search, local knowledge)
    QString context;
    const QString weatherCtx = fetchWeatherContext(trimmed);
    if (!weatherCtx.isEmpty()) {
        context.append(weatherCtx + QStringLiteral("\n\n"));
    }

    const QString webCtx = fetchWebSearchContext(trimmed);
    if (!webCtx.isEmpty()) {
        context.append(webCtx + QStringLiteral("\n\n"));
    }

    const QString localCtx = fetchDynamicLocalContext(trimmed, QStringLiteral("auto"));
    if (!localCtx.isEmpty()) {
        context.append(localCtx + QStringLiteral("\n\n"));
    }

    if (readConfigBool(QStringLiteral("knowledgeBaseEnabled"), true)) {
        const QString kbCtx = fetchKnowledgeBaseContext(trimmed);
        if (!kbCtx.isEmpty()) {
            context.append(kbCtx + QStringLiteral("\n\n"));
        }
    }

    // 3. Check vision action (screen vision request)
    QString base64Image;
    static const QRegularExpression screenRegex(QStringLiteral("(pantalla|screen|captura|mira lo que|analiza mi pantalla)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(screenRegex)) {
        const QString tmpShot = QDir::tempPath() + QStringLiteral("/kiri_vision_%1.png").arg(QDateTime::currentMSecsSinceEpoch());
        captureScreenToPng(tmpShot);
        base64Image = encodeImageToBase64(tmpShot);
        QFile::remove(tmpShot);
    }

    sendToOllama(trimmed, context, base64Image);
}

void KiriApplet::sendToOllama(const QString &userPrompt, const QString &injectedContext, const QString &base64Image)
{
    if (!m_ollamaReachable) {
        setState(QStringLiteral("error"), i18n("No se puede conectar con el servidor Ollama en %1.", resolveOllamaUrl()));
        return;
    }

    setState(QStringLiteral("thinking"), i18n("Generando respuesta…"));
    m_streamingText.clear();
    m_ttsSpeechBuffer.clear();
    Q_EMIT streamingTextChanged();

    const bool hasVision = !base64Image.isEmpty();
    QString modelToUse = hasVision ? resolveVisionModel() : resolveOllamaModel();
    if (!hasVision && modelToUse.contains(QStringLiteral("moondream"), Qt::CaseInsensitive)) {
        modelToUse = QStringLiteral("qwen2.5:7b");
    }

    // Build chat messages array
    QJsonArray chatMsgs;

    // System prompt
    QJsonObject sysObj;
    sysObj[QStringLiteral("role")] = QStringLiteral("system");
    QString fullSysPrompt = resolveSystemPrompt();
    if (!fullSysPrompt.contains(QStringLiteral("español"), Qt::CaseInsensitive)) {
        fullSysPrompt.prepend(QStringLiteral("REGLA ESTRICTA DE IDIOMA: Responde SIEMPRE y exclusivamente en español de España (castellano).\n\n"));
    }
    const QString vitals = collectSystemVitals();
    if (!vitals.isEmpty()) {
        fullSysPrompt.append(QStringLiteral("\n[System Environment: %1]").arg(vitals));
    }
    if (!injectedContext.isEmpty()) {
        fullSysPrompt.append(QStringLiteral("\n[Additional Relevant Context:\n%1]").arg(injectedContext.trimmed()));
    }
    sysObj[QStringLiteral("content")] = fullSysPrompt;
    chatMsgs.append(sysObj);

    // Recent history (last 8 messages)
    const int startIdx = qMax(0, m_messages.size() - 8);
    for (int i = startIdx; i < m_messages.size() - 1; ++i) {
        const QVariantMap vm = m_messages.at(i).toMap();
        QJsonObject mObj;
        mObj[QStringLiteral("role")] = vm.value(QStringLiteral("role")).toString();
        mObj[QStringLiteral("content")] = vm.value(QStringLiteral("content")).toString();
        chatMsgs.append(mObj);
    }

    // Current user message
    QJsonObject userObj;
    userObj[QStringLiteral("role")] = QStringLiteral("user");
    userObj[QStringLiteral("content")] = userPrompt;
    if (hasVision) {
        QJsonArray imgs;
        imgs.append(base64Image);
        userObj[QStringLiteral("images")] = imgs;
    }
    chatMsgs.append(userObj);

    // Ollama options
    QJsonObject opts;
    opts[QStringLiteral("temperature")] = readConfigDouble(QStringLiteral("ollamaTemperature"), 0.2);
    opts[QStringLiteral("num_ctx")] = readConfigInt(QStringLiteral("ollamaNumCtx"), 4096);
    opts[QStringLiteral("num_thread")] = readConfigInt(QStringLiteral("ollamaNumThread"), 6);
    opts[QStringLiteral("num_predict")] = readConfigInt(QStringLiteral("ollamaNumPredict"), 384);

    QJsonObject reqPayload;
    reqPayload[QStringLiteral("model")] = modelToUse;
    reqPayload[QStringLiteral("messages")] = chatMsgs;
    reqPayload[QStringLiteral("stream")] = true;
    reqPayload[QStringLiteral("options")] = opts;
    reqPayload[QStringLiteral("keep_alive")] = readConfigString(QStringLiteral("ollamaKeepAlive"), QStringLiteral("24h"));

    const QUrl chatUrl(resolveOllamaUrl() + QStringLiteral("/api/chat"));
    QNetworkRequest req(chatUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    if (m_currentOllamaReply) {
        m_currentOllamaReply->abort();
        m_currentOllamaReply->deleteLater();
    }

    m_currentOllamaReply = m_nam->post(req, QJsonDocument(reqPayload).toJson(QJsonDocument::Compact));
    connect(m_currentOllamaReply, &QNetworkReply::readyRead, this, &KiriApplet::handleOllamaStreamChunk);
    connect(m_currentOllamaReply, &QNetworkReply::finished, this, &KiriApplet::handleOllamaFinished);
}

void KiriApplet::handleOllamaStreamChunk()
{
    if (!m_currentOllamaReply) return;

    while (m_currentOllamaReply->canReadLine()) {
        const QByteArray line = m_currentOllamaReply->readLine().trimmed();
        if (line.isEmpty()) continue;

        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) continue;

        const QJsonObject obj = doc.object();
        const QJsonObject msgObj = obj.value(QStringLiteral("message")).toObject();
        const QString chunk = msgObj.value(QStringLiteral("content")).toString();

        if (!chunk.isEmpty()) {
            m_streamingText.append(chunk);
            Q_EMIT streamingTextChanged();
            processStreamingTtsChunk(chunk);
        }
    }
}

void KiriApplet::handleOllamaFinished()
{
    if (!m_currentOllamaReply) return;

    const bool error = (m_currentOllamaReply->error() != QNetworkReply::NoError &&
                        m_currentOllamaReply->error() != QNetworkReply::OperationCanceledError);

    m_currentOllamaReply->deleteLater();
    m_currentOllamaReply = nullptr;

    if (error) {
        setState(QStringLiteral("error"), i18n("Error al comunicarse con Ollama."));
        return;
    }

    QString finalResponse = m_streamingText.trimmed();
    // Strip reasoning tags (<think>...</think>) if present
    static const QRegularExpression thinkRegex(QStringLiteral("<think>.*?</think>"), QRegularExpression::DotMatchesEverythingOption);
    finalResponse.remove(thinkRegex);
    finalResponse = finalResponse.trimmed();

    if (!finalResponse.isEmpty()) {
        QVariantMap assistantMsg;
        assistantMsg[QStringLiteral("role")] = QStringLiteral("assistant");
        assistantMsg[QStringLiteral("content")] = finalResponse;
        assistantMsg[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);
        m_messages.append(assistantMsg);
        Q_EMIT messagesChanged();
        saveConversationHistory();

        // Flush any remaining text to TTS
        if (!m_ttsSpeechBuffer.isEmpty()) {
            enqueueSentenceForSpeech(m_ttsSpeechBuffer.trimmed());
            m_ttsSpeechBuffer.clear();
        }
    }

    m_streamingText.clear();
    Q_EMIT streamingTextChanged();

    if (m_isSpeakingSentence || !m_ttsQueue.isEmpty()) {
        setState(QStringLiteral("speaking"), i18n("Reproduciendo voz…"));
    } else {
        setState(QStringLiteral("idle"), QString());
    }
}

void KiriApplet::stopGeneration()
{
    if (m_currentOllamaReply) {
        m_currentOllamaReply->abort();
        m_currentOllamaReply->deleteLater();
        m_currentOllamaReply = nullptr;
    }
    stopSpeaking();
    setState(QStringLiteral("idle"), QString());
}

void KiriApplet::clearHistory()
{
    m_messages.clear();
    Q_EMIT messagesChanged();
    QFile::remove(historyFilePath());
    setState(QStringLiteral("idle"), QString());
}

// =========================================================================
// Voice & Audio Processing (Whisper.cpp + PipeWire)
// =========================================================================

void KiriApplet::startRecording()
{
    if (m_state == QLatin1String("listening")) {
        return;
    }

    const QString pwRecord = QStandardPaths::findExecutable(QStringLiteral("pw-record"));
    if (pwRecord.isEmpty()) {
        setState(QStringLiteral("error"), i18n("PipeWire (pw-record) no fue encontrado en este sistema."));
        return;
    }

    stopWakeWordListener();
    stopSpeaking();

    m_currentRecordingPath = QDir::tempPath() + QStringLiteral("/kiri_rec_%1.wav").arg(QDateTime::currentMSecsSinceEpoch());

    m_recordProcess = new QProcess(this);
    QStringList args;
    args << QStringLiteral("--format=s16")
         << QStringLiteral("--rate=16000")
         << QStringLiteral("--channels=1")
         << m_currentRecordingPath;

    m_recordProcess->start(pwRecord, args);
    if (!m_recordProcess->waitForStarted(2000)) {
        setState(QStringLiteral("error"), i18n("No se pudo iniciar la grabación de audio."));
        m_recordProcess->deleteLater();
        m_recordProcess = nullptr;
        return;
    }

    setState(QStringLiteral("listening"), i18n("Escuchando… Habla ahora, Kiri detectará cuando termines."));
    startVad();
}

void KiriApplet::stopRecording()
{
    stopVad();

    if (m_recordProcess) {
        if (m_recordProcess->state() == QProcess::Running) {
            m_recordProcess->terminate();
            if (!m_recordProcess->waitForFinished(1500)) {
                m_recordProcess->kill();
            }
        }
        m_recordProcess->deleteLater();
        m_recordProcess = nullptr;
    }

    if (m_state == QLatin1String("listening")) {
        setState(QStringLiteral("transcribing"), i18n("Transcribiendo voz…"));
        runWhisperTranscription();
    }
}

void KiriApplet::toggleRecording()
{
    if (m_state == QLatin1String("listening")) {
        stopRecording();
    } else {
        startRecording();
    }
}

void KiriApplet::startVad()
{
    m_speechDetected = false;
    m_speechSilenceTimer.invalidate();
    m_vadTimer->start(100); // Check file growth / volume every 100ms
}

void KiriApplet::stopVad()
{
    m_vadTimer->stop();
    m_audioLevel = 0.0;
    Q_EMIT audioLevelChanged();
}

void KiriApplet::checkVoiceActivity()
{
    if (!m_recordProcess || m_currentRecordingPath.isEmpty()) return;

    QFile file(m_currentRecordingPath);
    if (!file.open(QIODevice::ReadOnly)) return;

    const qint64 fileSize = file.size();
    if (fileSize <= 44) return; // Wait for WAV header to finish

    // Read the most recent ~100ms of 16kHz 16-bit mono PCM (3200 bytes)
    const qint64 bytesToRead = qMin<qint64>(fileSize - 44, 3200);
    if (!file.seek(fileSize - bytesToRead)) return;

    const QByteArray chunk = file.read(bytesToRead);
    const int sampleCount = chunk.size() / sizeof(qint16);
    if (sampleCount <= 0) return;

    const qint16 *samples = reinterpret_cast<const qint16 *>(chunk.constData());
    double sumSquares = 0.0;
    for (int i = 0; i < sampleCount; ++i) {
        const double norm = samples[i] / 32768.0;
        sumSquares += norm * norm;
    }
    const double rms = std::sqrt(sumSquares / sampleCount);

    // Audio level normalized for reactive Siri orb animation (0.0 to 1.0)
    const qreal newLevel = qBound(0.0, rms * 4.5, 1.0);
    if (std::abs(m_audioLevel - newLevel) > 0.015) {
        m_audioLevel = newLevel;
        Q_EMIT audioLevelChanged();
    }

    // Voice Activity Detection (audible speech typically > 0.025 RMS)
    const double speechRmsThreshold = 0.025;
    const bool isSpeakingNow = (rms >= speechRmsThreshold);

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
            // Safety guard: stop after 7 seconds if no voice was ever detected
            if (fileSize > static_cast<qint64>(16000 * sizeof(qint16) * 7)) {
                stopRecording();
            }
        }
    }
}

void KiriApplet::runWhisperTranscription()
{
    QFileInfo fi(m_currentRecordingPath);
    if (!fi.exists() || fi.size() < 1024) {
        setState(QStringLiteral("idle"), i18n("No se detectó audio de voz."));
        QFile::remove(m_currentRecordingPath);
        return;
    }

    QString whisperCli = QStandardPaths::findExecutable(QStringLiteral("whisper-cli"));
    if (whisperCli.isEmpty()) {
        whisperCli = QStandardPaths::findExecutable(QStringLiteral("whisper"));
    }
    const QString modelPath = resolveWhisperModelPath();

    if (whisperCli.isEmpty() || modelPath.isEmpty()) {
        setState(QStringLiteral("error"), i18n("Whisper.cpp o su modelo no están instalados. Ejecuta scripts/setup-models.sh."));
        QFile::remove(m_currentRecordingPath);
        return;
    }

    QProcess *whisperProc = new QProcess(this);
    QStringList args;
    args << QStringLiteral("-m") << modelPath
         << QStringLiteral("-f") << m_currentRecordingPath
         << QStringLiteral("--no-timestamps")
         << QStringLiteral("-l") << resolveWhisperLanguage();

    const QString wavToClean = m_currentRecordingPath;

    connect(whisperProc, &QProcess::finished, this, [this, whisperProc, wavToClean](int exitCode, QProcess::ExitStatus) {
        whisperProc->deleteLater();
        QFile::remove(wavToClean);

        if (exitCode != 0) {
            setState(QStringLiteral("error"), i18n("Error al transcribir el audio."));
            return;
        }

        QString text = QString::fromUtf8(whisperProc->readAllStandardOutput()).trimmed();
        // Clean bracketed artifacts like [BLANK_AUDIO] or (música)
        static const QRegularExpression bracketRegex(QStringLiteral("\\[.*?\\]|\\(.*?\\)"));
        text.remove(bracketRegex);
        text = text.trimmed();

        if (text.isEmpty()) {
            setState(QStringLiteral("idle"), i18n("No se reconoció ninguna frase."));
            return;
        }

        Q_EMIT transcriptionReady(text);
        sendMessage(text, true);
    });

    whisperProc->start(whisperCli, args);
}

// =========================================================================
// Text-to-Speech (Piper TTS + PipeWire)
// =========================================================================

void KiriApplet::speakText(const QString &text)
{
    stopSpeaking();
    const QString clean = stripMarkdownForSpeech(text);
    if (!clean.isEmpty()) {
        enqueueSentenceForSpeech(clean);
    }
}

void KiriApplet::stopSpeaking()
{
    m_ttsQueue.clear();
    m_ttsSpeechBuffer.clear();
    if (m_ttsProcess) {
        if (m_ttsProcess->state() == QProcess::Running) {
            m_ttsProcess->kill();
        }
        m_ttsProcess->deleteLater();
        m_ttsProcess = nullptr;
    }
    m_isSpeakingSentence = false;
    if (m_state == QLatin1String("speaking")) {
        setState(QStringLiteral("idle"), QString());
    }
}

void KiriApplet::processStreamingTtsChunk(const QString &newChunk)
{
    const QString speechMode = resolveSpeechOutputMode();
    if (speechMode == QLatin1String("manual")) {
        return;
    }
    if (speechMode == QLatin1String("voice_only") && !m_lastQueryWasVoice) {
        return;
    }

    m_ttsSpeechBuffer.append(newChunk);

    // Look for sentence terminators: . ? ! \n
    static const QRegularExpression endRegex(QStringLiteral("([.?!\\n]+)"));
    QRegularExpressionMatch match = endRegex.match(m_ttsSpeechBuffer);
    while (match.hasMatch()) {
        const int endPos = match.capturedEnd();
        const QString sentence = m_ttsSpeechBuffer.left(endPos).trimmed();
        m_ttsSpeechBuffer = m_ttsSpeechBuffer.mid(endPos);

        const QString clean = stripMarkdownForSpeech(sentence);
        if (!clean.isEmpty()) {
            enqueueSentenceForSpeech(clean);
        }
        match = endRegex.match(m_ttsSpeechBuffer);
    }
}

void KiriApplet::enqueueSentenceForSpeech(const QString &sentence)
{
    m_ttsQueue.enqueue(sentence);
    if (!m_isSpeakingSentence) {
        playNextTtsQueueItem();
    }
}

void KiriApplet::playNextTtsQueueItem()
{
    if (m_ttsQueue.isEmpty()) {
        m_isSpeakingSentence = false;
        if (m_state == QLatin1String("speaking")) {
            setState(QStringLiteral("idle"), QString());
        }
        return;
    }

    const QString sentence = m_ttsQueue.dequeue();
    speakSentence(sentence);
}

void KiriApplet::speakSentence(const QString &sentence)
{
    const QString piperCli = QStandardPaths::findExecutable(QStringLiteral("piper"));
    const QString voicePath = resolvePiperVoicePath();
    const QString pwPlay = QStandardPaths::findExecutable(QStringLiteral("pw-play"));

    if (piperCli.isEmpty() || voicePath.isEmpty() || pwPlay.isEmpty() || sentence.trimmed().isEmpty()) {
        m_isSpeakingSentence = false;
        playNextTtsQueueItem();
        return;
    }

    m_isSpeakingSentence = true;
    setState(QStringLiteral("speaking"), i18n("Reproduciendo respuesta…"));

    // Pipeline: piper --model voicePath --output_file - | pw-play -
    m_ttsProcess = new QProcess(this);
    QProcess *pwPlayProcess = new QProcess(m_ttsProcess);

    m_ttsProcess->setStandardOutputProcess(pwPlayProcess);

    QStringList piperArgs;
    piperArgs << QStringLiteral("--model") << voicePath
              << QStringLiteral("--output_file") << QStringLiteral("-");

    QStringList pwArgs;
    pwArgs << QStringLiteral("-");

    connect(pwPlayProcess, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        if (m_ttsProcess) {
            m_ttsProcess->deleteLater();
            m_ttsProcess = nullptr;
        }
        playNextTtsQueueItem();
    });

    m_ttsProcess->start(piperCli, piperArgs);
    pwPlayProcess->start(pwPlay, pwArgs);

    m_ttsProcess->write(sentence.toUtf8());
    m_ttsProcess->closeWriteChannel();
}

QString KiriApplet::stripMarkdownForSpeech(const QString &markdown) const
{
    QString res = markdown;
    // Remove code blocks
    static const QRegularExpression codeRegex(QStringLiteral("```[\\s\\S]*?```|`.*?`"));
    res.remove(codeRegex);
    // Remove links [text](url) -> text
    static const QRegularExpression linkRegex(QStringLiteral("\\[([^\\]]+)\\]\\([^\\)]+\\)"));
    res.replace(linkRegex, QStringLiteral("\\1"));
    // Remove markdown formatting symbols
    static const QRegularExpression symRegex(QStringLiteral("[*#_>~]"));
    res.remove(symRegex);
    return res.trimmed();
}

// =========================================================================
// Wake Word Listener ("Hola Kiri" / "Oye Kiri" / "Hey Kiri")
// =========================================================================

void KiriApplet::startWakeWordListener()
{
    if (!m_wakeWordEnabled) {
        return;
    }

    if (m_state == QLatin1String("listening") || m_state == QLatin1String("speaking")) {
        return;
    }

    if (m_wakeWordProcess && m_wakeWordProcess->state() == QProcess::Running) {
        return;
    }

    const QString pwRecord = QStandardPaths::findExecutable(QStringLiteral("pw-record"));
    if (pwRecord.isEmpty()) {
        qWarning() << "Kiri: pw-record not found in PATH for wake word listener";
        return;
    }

    m_wakeWordPcmBuffer.clear();
    m_wakeWordEvaluating = false;

    if (!m_wakeWordProcess) {
        m_wakeWordProcess = new QProcess(this);
        connect(m_wakeWordProcess, &QProcess::readyReadStandardOutput, this, &KiriApplet::onWakeWordAudioChunkReady);
    }

    QStringList args;
    args << QStringLiteral("--format=s16")
         << QStringLiteral("--rate=16000")
         << QStringLiteral("--channels=1")
         << QStringLiteral("--container=raw")
         << QStringLiteral("-");

    m_wakeWordProcess->start(pwRecord, args);
    if (!m_wakeWordProcess->waitForStarted(2000)) {
        qWarning() << "Kiri: Failed to start pw-record for wake word listener";
    } else {
        qInfo() << "Kiri: Wake word background listener started successfully (PID:" << m_wakeWordProcess->processId() << ")";
    }
}

void KiriApplet::stopWakeWordListener()
{
    if (m_wakeWordProcess) {
        if (m_wakeWordProcess->state() == QProcess::Running) {
            m_wakeWordProcess->terminate();
            if (!m_wakeWordProcess->waitForFinished(500)) {
                m_wakeWordProcess->kill();
            }
        }
    }
    m_wakeWordPcmBuffer.clear();
    m_wakeWordEvaluating = false;
}

void KiriApplet::onWakeWordAudioChunkReady()
{
    if (!m_wakeWordProcess) return;

    const QByteArray chunk = m_wakeWordProcess->readAllStandardOutput();
    if (chunk.isEmpty()) return;

    m_wakeWordPcmBuffer.append(chunk);

    // Keep circular buffer to ~2.5 seconds (16000 samples/sec * 2 bytes = 32000 B/s -> 80000 B)
    if (m_wakeWordPcmBuffer.size() > 80000) {
        m_wakeWordPcmBuffer = m_wakeWordPcmBuffer.right(80000);
    }

    if (m_wakeWordEvaluating) return;

    // Fast RMS volume check on the incoming audio chunk
    const int sampleCount = chunk.size() / sizeof(qint16);
    if (sampleCount <= 0) return;

    const qint16 *samples = reinterpret_cast<const qint16 *>(chunk.constData());
    double sumSquares = 0.0;
    for (int i = 0; i < sampleCount; ++i) {
        const double s = samples[i] / 32768.0;
        sumSquares += s * s;
    }
    const double rms = std::sqrt(sumSquares / sampleCount);

    // Voice detected above background noise (>= 0.016) and not in cooldown
    if (rms >= 0.016 && (!m_wakeWordCooldown.isValid() || m_wakeWordCooldown.elapsed() > 1200)) {
        if (m_wakeWordPcmBuffer.size() >= 24000) { // At least ~0.75s buffered
            m_wakeWordEvaluating = true;
            qInfo() << "Kiri: Speech detected in background stream (RMS:" << rms << "), capturing wake word window...";

            // Allow 650ms for user to finish saying "Hola Kiri"
            QTimer::singleShot(650, this, [this]() {
                if (!m_wakeWordEnabled || m_state != QLatin1String("idle")) {
                    m_wakeWordEvaluating = false;
                    return;
                }
                const QString tmpWav = QDir::tempPath() + QStringLiteral("/kiri_ww_probe_%1.wav").arg(QDateTime::currentMSecsSinceEpoch());
                QFile f(tmpWav);
                if (f.open(QIODevice::WriteOnly)) {
                    const QByteArray pcmData = m_wakeWordPcmBuffer.right(64000); // last ~2 seconds
                    const QByteArray header = createWavHeader(pcmData.size(), 16000, 1, 16);
                    f.write(header);
                    f.write(pcmData);
                    f.close();
                    checkWakeWordTranscription(tmpWav);
                } else {
                    m_wakeWordEvaluating = false;
                }
            });
        }
    }
}

void KiriApplet::checkWakeWordTranscription(const QString &wavPath)
{
    QString whisperCli = QStandardPaths::findExecutable(QStringLiteral("whisper-cli"));
    if (whisperCli.isEmpty()) {
        whisperCli = QStandardPaths::findExecutable(QStringLiteral("whisper"));
    }
    const QString modelPath = resolveWhisperModelPath();

    if (whisperCli.isEmpty() || modelPath.isEmpty()) {
        QFile::remove(wavPath);
        m_wakeWordEvaluating = false;
        return;
    }

    QProcess *whisperProc = new QProcess(this);
    QStringList args;
    args << QStringLiteral("-m") << modelPath
         << QStringLiteral("-f") << wavPath
         << QStringLiteral("-l") << QStringLiteral("es")
         << QStringLiteral("--prompt") << QStringLiteral("Hola Kiri, Oye Kiri, Hey Kiri")
         << QStringLiteral("--no-timestamps")
         << QStringLiteral("-t") << QStringLiteral("4")
         << QStringLiteral("-np");

    const QString wavToClean = wavPath;

    connect(whisperProc, &QProcess::finished, this, [this, whisperProc, wavToClean](int exitCode, QProcess::ExitStatus) {
        whisperProc->deleteLater();
        QFile::remove(wavToClean);
        m_wakeWordEvaluating = false;
        m_wakeWordCooldown.start();

        if (exitCode != 0) return;

        QString raw = QString::fromUtf8(whisperProc->readAllStandardOutput()).trimmed();
        static const QRegularExpression bracketRegex(QStringLiteral("\\[.*?\\]|\\(.*?\\)"));
        raw.remove(bracketRegex);
        const QString cleaned = raw.trimmed().toLower();

        qInfo() << "Kiri: Wake word probe transcription result:" << cleaned;

        static const QRegularExpression wwRegex(
            QStringLiteral("(?:hola|oye|hey|ok|quiri|ciri|kili)?\\s*kiri|hola\\s*quiri"),
            QRegularExpression::CaseInsensitiveOption);

        if (cleaned.contains(wwRegex) || cleaned.contains(QStringLiteral("kiri")) || cleaned.contains(QStringLiteral("quiri"))) {
            qInfo() << "Kiri: WAKE WORD ACTIVATED! ('" << cleaned << "')";

            KNotification::event(QStringLiteral("notification"),
                                 i18n("Kiri"),
                                 i18n("¡Hola! Te escucho…"));

            stopWakeWordListener();
            Q_EMIT wakeWordTriggered();

            QTimer::singleShot(250, this, [this]() {
                startRecording();
            });
        }
    });

    whisperProc->start(whisperCli, args);
}

// =========================================================================
// System Actions & Personal Assistant Capabilities
// =========================================================================

bool KiriApplet::detectAndExecuteSystemAction(const QString &input)
{
    const QString trimmed = input.trimmed();

    // 1. App Launch: "abre/inicia/lanza/ejecuta/arranca <app>"
    static const QRegularExpression launchRegex(
        QStringLiteral("^(?:por\\s+favor\\s+)?(?:abre|inicia|lanza|ejecuta|arranca|abrir)\\s+(.+?)[.!?¿¡]*$"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch lMatch = launchRegex.match(trimmed);
    if (lMatch.hasMatch()) {
        const QString appQuery = lMatch.captured(1).trimmed();
        QString launchedName;
        if (launchApplication(appQuery, &launchedName)) {
            const QString resp = i18n("Abriendo %1.", launchedName.isEmpty() ? appQuery : launchedName);
            sendMessage(resp, false);
            speakText(resp);
            return true;
        }
    }

    // 2. App Close: "cierra/termina/mata/cerrar <app>"
    static const QRegularExpression closeRegex(
        QStringLiteral("^(?:por\\s+favor\\s+)?(?:cierra|termina|mata|cerrar)\\s+(.+?)[.!?¿¡]*$"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch cMatch = closeRegex.match(trimmed);
    if (cMatch.hasMatch()) {
        const QString appQuery = cMatch.captured(1).trimmed();
        QString closedName;
        if (closeApplication(appQuery, &closedName)) {
            const QString resp = i18n("Cerrando %1.", closedName.isEmpty() ? appQuery : closedName);
            sendMessage(resp, false);
            speakText(resp);
            return true;
        }
    }

    // 3. Volume Control via PipeWire / WirePlumber (wpctl)
    static const QRegularExpression volUpRegex(QStringLiteral("(?:sube|subir|aumenta|aumentar|sube el|subir el)\\s+(?:volumen|sonido|audio)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(volUpRegex)) {
        QProcess::execute(QStringLiteral("wpctl"), QStringList() << QStringLiteral("set-volume") << QStringLiteral("@DEFAULT_AUDIO_SINK@") << QStringLiteral("5%+"));
        const QString resp = i18n("Subiendo el volumen.");
        sendMessage(resp, false);
        speakText(resp);
        return true;
    }

    static const QRegularExpression volDownRegex(QStringLiteral("(?:baja|bajar|disminuye|disminuir|baja el|bajar el)\\s+(?:volumen|sonido|audio)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(volDownRegex)) {
        QProcess::execute(QStringLiteral("wpctl"), QStringList() << QStringLiteral("set-volume") << QStringLiteral("@DEFAULT_AUDIO_SINK@") << QStringLiteral("5%-"));
        const QString resp = i18n("Bajando el volumen.");
        sendMessage(resp, false);
        speakText(resp);
        return true;
    }

    static const QRegularExpression volMuteRegex(QStringLiteral("(?:silencia|silenciar|mutea|mutear|quitar)\\s+(?:el\\s+)?(?:volumen|sonido|audio)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(volMuteRegex) || trimmed.compare(QStringLiteral("silencio"), Qt::CaseInsensitive) == 0) {
        QProcess::execute(QStringLiteral("wpctl"), QStringList() << QStringLiteral("set-mute") << QStringLiteral("@DEFAULT_AUDIO_SINK@") << QStringLiteral("1"));
        const QString resp = i18n("Audio silenciado.");
        sendMessage(resp, false);
        speakText(resp);
        return true;
    }

    static const QRegularExpression volUnmuteRegex(QStringLiteral("(?:activa|activar|desactiva el silencio|reactiva)\\s+(?:el\\s+)?(?:volumen|sonido|audio)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(volUnmuteRegex)) {
        QProcess::execute(QStringLiteral("wpctl"), QStringList() << QStringLiteral("set-mute") << QStringLiteral("@DEFAULT_AUDIO_SINK@") << QStringLiteral("0"));
        const QString resp = i18n("Audio reactivado.");
        sendMessage(resp, false);
        speakText(resp);
        return true;
    }

    // 4. Media Playback Control via playerctl
    static const QRegularExpression mediaPlayRegex(QStringLiteral("(?:reproduce|reanuda|play)\\s+(?:la\\s+)?(?:m[uú]sica|canci[oó]n|audio)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(mediaPlayRegex) || trimmed.compare(QStringLiteral("reproducir"), Qt::CaseInsensitive) == 0) {
        QProcess::execute(QStringLiteral("playerctl"), QStringList() << QStringLiteral("play"));
        const QString resp = i18n("Reanudando reproducción.");
        sendMessage(resp, false);
        speakText(resp);
        return true;
    }

    static const QRegularExpression mediaPauseRegex(QStringLiteral("(?:pausa|pausar|para|parar)\\s+(?:la\\s+)?(?:m[uú]sica|canci[oó]n|audio)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(mediaPauseRegex) || trimmed.compare(QStringLiteral("pausa"), Qt::CaseInsensitive) == 0) {
        QProcess::execute(QStringLiteral("playerctl"), QStringList() << QStringLiteral("pause"));
        const QString resp = i18n("Pausando reproducción.");
        sendMessage(resp, false);
        speakText(resp);
        return true;
    }

    static const QRegularExpression mediaNextRegex(QStringLiteral("(?:siguiente|pasa\\s+de)\\s+(?:canci[oó]n|pista)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(mediaNextRegex)) {
        QProcess::execute(QStringLiteral("playerctl"), QStringList() << QStringLiteral("next"));
        const QString resp = i18n("Siguiente pista.");
        sendMessage(resp, false);
        speakText(resp);
        return true;
    }

    static const QRegularExpression mediaPrevRegex(QStringLiteral("(?:anterior|atr[aá]s)\\s+(?:canci[oó]n|pista)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(mediaPrevRegex)) {
        QProcess::execute(QStringLiteral("playerctl"), QStringList() << QStringLiteral("previous"));
        const QString resp = i18n("Pista anterior.");
        sendMessage(resp, false);
        speakText(resp);
        return true;
    }

    // 5. Screen Lock
    static const QRegularExpression lockScreenRegex(QStringLiteral("(?:bloquea|bloquear)\\s+(?:la\\s+)?(?:pantalla|sesi[oó]n|equipo)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(lockScreenRegex)) {
        QProcess::startDetached(QStringLiteral("qdbus6"), QStringList() << QStringLiteral("org.freedesktop.ScreenSaver") << QStringLiteral("/ScreenSaver") << QStringLiteral("Lock"));
        const QString resp = i18n("Bloqueando la pantalla.");
        sendMessage(resp, false);
        speakText(resp);
        return true;
    }

    // 6. Screenshot Tool (Spectacle)
    static const QRegularExpression shotActionRegex(QStringLiteral("^(?:haz\\s+una\\s+|toma\\s+una\\s+)?(?:captura(?:\\s+de\\s+pantalla)?|pantallazo)[.!?¿¡]*$"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(shotActionRegex) && !trimmed.contains(QStringLiteral("analiza")) && !trimmed.contains(QStringLiteral("mira"))) {
        QProcess::startDetached(QStringLiteral("spectacle"), QStringList());
        const QString resp = i18n("Abriendo herramienta de captura de pantalla.");
        sendMessage(resp, false);
        speakText(resp);
        return true;
    }

    // 7. Power Profile: "modo ahorro/rendimiento/equilibrado"
    static const QRegularExpression setPowerRegex(QStringLiteral("(?:pon|cambia a|activa el)?\\s*(?:modo)?\\s*(rendimiento|ahorro|equilibrado)"), QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch pMatch = setPowerRegex.match(trimmed);
    if (pMatch.hasMatch()) {
        const QString modeStr = pMatch.captured(1).toLower();
        QString targetProfile = QStringLiteral("balanced");
        if (modeStr.contains(QStringLiteral("rendimiento"))) targetProfile = QStringLiteral("performance");
        else if (modeStr.contains(QStringLiteral("ahorro"))) targetProfile = QStringLiteral("power-saver");

        if (setActivePowerProfile(targetProfile)) {
            const QString resp = i18n("Perfil de energía cambiado a %1.", modeStr);
            sendMessage(resp, false);
            speakText(resp);
            return true;
        }
    }

    // 8. Do Not Disturb: "activa/desactiva no molestar"
    static const QRegularExpression enableDndRegex(QStringLiteral("(?:activa|pon|habilita)\\s+(?:el\\s+modo\\s+)?no\\s+molestar"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(enableDndRegex)) {
        setDoNotDisturb(true);
        const QString resp = i18n("Modo No Molestar activado.");
        sendMessage(resp, false);
        speakText(resp);
        return true;
    }

    static const QRegularExpression disableDndRegex(QStringLiteral("(?:desactiva|quita|deshabilita)\\s+(?:el\\s+modo\\s+)?no\\s+molestar"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(disableDndRegex)) {
        setDoNotDisturb(false);
        const QString resp = i18n("Modo No Molestar desactivado.");
        sendMessage(resp, false);
        speakText(resp);
        return true;
    }

    // 9. Battery Status: "¿cuánta batería me queda?"
    static const QRegularExpression batteryRegex(QStringLiteral("(bater[ií]a|nivel de carga)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(batteryRegex) && trimmed.contains(QStringLiteral("?"))) {
        const QString bInfo = getLocalBatteryInfo();
        if (!bInfo.isEmpty()) {
            sendMessage(bInfo, false);
            speakText(bInfo);
            return true;
        }
    }

    // 10. Timers: "temporizador de X minutos/segundos"
    static const QRegularExpression timerRegex(QStringLiteral("(?:pon\\s+un\\s+)?temporizador\\s+de\\s+(\\d+)\\s*(segundos?|minutos?|horas?)"), QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch tMatch = timerRegex.match(trimmed);
    if (tMatch.hasMatch()) {
        const int val = tMatch.captured(1).toInt();
        const QString unit = tMatch.captured(2).toLower();
        int msecs = val * 1000;
        if (unit.startsWith(QStringLiteral("min"))) msecs *= 60;
        else if (unit.startsWith(QStringLiteral("hor"))) msecs *= 3600;

        QTimer::singleShot(msecs, this, [this, val, unit]() {
            KNotification::event(QStringLiteral("notification"),
                                 i18n("Temporizador finalizado"),
                                 i18n("¡Tu temporizador de %1 %2 ha terminado!", val, unit));
            speakText(i18n("¡Tu temporizador de %1 %2 ha terminado!", val, unit));
        });

        const QString resp = i18n("Temporizador establecido para %1 %2.", val, unit);
        sendMessage(resp, false);
        speakText(resp);
        return true;
    }

    // 11. KDE Connect Find My Phone: "haz sonar mi móvil/teléfono"
    static const QRegularExpression findPhoneRegex(QStringLiteral("(?:haz\\s+sonar|encuentra|busca|d[oó]nde\\s+est[aá])\\s+(?:mi\\s+)?(?:tel[eé]fono|m[oó]vil|dispositivo)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(findPhoneRegex)) {
        const auto devices = getKdeConnectDevices();
        if (!devices.isEmpty()) {
            if (ringKdeConnectDevice(devices.first().id)) {
                const QString resp = i18n("Haciendo sonar tu teléfono (%1)…", devices.first().name);
                sendMessage(resp, false);
                speakText(resp);
                return true;
            }
        }
    }

    // 12. KDE Connect Battery: "batería del móvil/teléfono"
    static const QRegularExpression phoneBatteryRegex(QStringLiteral("(?:bater[ií]a|carga)\\s+(?:de|del|en\\s+el)?\\s*(?:tel[eé]fono|m[oó]vil)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(phoneBatteryRegex)) {
        const auto devices = getKdeConnectDevices();
        if (!devices.isEmpty()) {
            const QString bStr = getKdeConnectBattery(devices.first().id);
            if (!bStr.isEmpty()) {
                sendMessage(bStr, false);
                speakText(bStr);
                return true;
            }
        }
    }

    // 13. Calendar Events: "crea/añade/pon evento ..."
    static const QRegularExpression calRegex(QStringLiteral("(?:crea|a[ñn]ade|pon)\\s+(?:un\\s+)?evento\\s+(.+)"), QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch calMatch = calRegex.match(trimmed);
    if (calMatch.hasMatch()) {
        const QString evPrompt = calMatch.captured(1).trimmed();
        if (createCalendarEvent(evPrompt, QString())) {
            const QString resp = i18n("Evento registrado en el calendario: %1.", evPrompt);
            sendMessage(resp, false);
            speakText(resp);
            return true;
        }
    }

    // 14. Email Composition: "envía correo a X diciendo Y"
    static const QRegularExpression emailRegex(QStringLiteral("(?:env[ií]a|redacta|escribe)\\s+(?:un\\s+)?(?:correo|email|mensaje)\\s+a\\s+([^\\s]+)(?:\\s+(?:diciendo|con|asunto)\\s+(.+))?"), QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch emMatch = emailRegex.match(trimmed);
    if (emMatch.hasMatch()) {
        const QString to = emMatch.captured(1).trimmed();
        const QString body = emMatch.captured(2).trimmed();
        const QString client = readConfigString(QStringLiteral("defaultEmailClient"), QStringLiteral("proton"));
        composeEmail(to, i18n("Mensaje de Kiri"), body, client);
        const QString resp = i18n("Abriendo cliente de correo para enviar mensaje a %1.", to);
        sendMessage(resp, false);
        speakText(resp);
        return true;
    }

    return false;
}

bool KiriApplet::launchApplication(const QString &appQuery, QString *launchedNameOut)
{
    QString query = appQuery.trimmed();
    query.remove(QRegularExpression(QStringLiteral("[.!?,;¿¡]+$")));
    query = query.trimmed();

    QString clean = normalizeForMatch(query);
    clean.remove(QRegularExpression(QStringLiteral("^(?:el|la|los|las|un|una|unos|unas|the|a|an)\\s+")));
    clean = clean.trimmed();

    if (clean.isEmpty()) {
        return false;
    }

    // 1. Semantic alias: Terminal Emulator
    static const QRegularExpression termRegex(QStringLiteral("^(?:terminal|consola|kterminal|konsole|shell|linea\\s+de\\s+comandos)$"), QRegularExpression::CaseInsensitiveOption);
    if (clean.contains(termRegex)) {
        if (launchedNameOut) *launchedNameOut = QStringLiteral("la terminal");
        if (QProcess::startDetached(QStringLiteral("konsole"), QStringList())) {
            return true;
        }
        return QProcess::startDetached(QStringLiteral("gtk-launch"), QStringList() << QStringLiteral("org.kde.konsole"));
    }

    // 2. Semantic alias: Default Web Browser
    static const QRegularExpression browserRegex(QStringLiteral("^(?:navegador|navegador\\s+web|internet|browser|web)$"), QRegularExpression::CaseInsensitiveOption);
    if (clean.contains(browserRegex)) {
        if (launchedNameOut) *launchedNameOut = QStringLiteral("el navegador web");
        QProcess proc;
        proc.start(QStringLiteral("xdg-settings"), QStringList() << QStringLiteral("get") << QStringLiteral("default-web-browser"));
        if (proc.waitForFinished(1000) && proc.exitCode() == 0) {
            QString desktop = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
            if (!desktop.isEmpty()) {
                if (desktop.endsWith(QStringLiteral(".desktop"))) {
                    desktop.chop(8);
                }
                if (QProcess::startDetached(QStringLiteral("gtk-launch"), QStringList() << desktop)) {
                    return true;
                }
            }
        }
        return QProcess::startDetached(QStringLiteral("xdg-open"), QStringList() << QStringLiteral("https://"));
    }

    // 3. Semantic alias: File Manager
    static const QRegularExpression fmRegex(QStringLiteral("^(?:archivos|mis\\s+archivos|gestor\\s+de\\s+archivos|carpetas|carpeta\\s+personal|dolphin)$"), QRegularExpression::CaseInsensitiveOption);
    if (clean.contains(fmRegex)) {
        if (launchedNameOut) *launchedNameOut = QStringLiteral("el gestor de archivos");
        return QProcess::startDetached(QStringLiteral("dolphin"), QStringList());
    }

    // 4. Semantic alias: System Settings
    static const QRegularExpression settingsRegex(QStringLiteral("^(?:ajustes|configuraci[oó]n|preferencias|preferencias\\s+del\\s+sistema|systemsettings)$"), QRegularExpression::CaseInsensitiveOption);
    if (clean.contains(settingsRegex)) {
        if (launchedNameOut) *launchedNameOut = QStringLiteral("las Preferencias del Sistema");
        return QProcess::startDetached(QStringLiteral("systemsettings"), QStringList());
    }

    // 5. Semantic alias: Calculator
    static const QRegularExpression calcRegex(QStringLiteral("^(?:calculadora|kcalc)$"), QRegularExpression::CaseInsensitiveOption);
    if (clean.contains(calcRegex)) {
        if (launchedNameOut) *launchedNameOut = QStringLiteral("la calculadora");
        return QProcess::startDetached(QStringLiteral("kcalc"), QStringList());
    }

    // 6. Semantic alias: System Monitor
    static const QRegularExpression monitorRegex(QStringLiteral("^(?:monitor\\s+del\\s+sistema|administrador\\s+de\\s+tareas|procesos|recursos)$"), QRegularExpression::CaseInsensitiveOption);
    if (clean.contains(monitorRegex)) {
        if (launchedNameOut) *launchedNameOut = QStringLiteral("el Monitor del Sistema");
        if (QProcess::startDetached(QStringLiteral("plasma-systemmonitor"), QStringList())) {
            return true;
        }
        return QProcess::startDetached(QStringLiteral("ksysguard"), QStringList());
    }

    // 7. Semantic alias: Text Editor
    static const QRegularExpression editorRegex(QStringLiteral("^(?:editor|editor\\s+de\\s+texto|notas|kwrite|kate)$"), QRegularExpression::CaseInsensitiveOption);
    if (clean.contains(editorRegex)) {
        if (launchedNameOut) *launchedNameOut = QStringLiteral("el editor de texto");
        if (QProcess::startDetached(QStringLiteral("kwrite"), QStringList())) {
            return true;
        }
        return QProcess::startDetached(QStringLiteral("kate"), QStringList());
    }

    // 8. Semantic alias: Software Store / Discover
    static const QRegularExpression discoverRegex(QStringLiteral("^(?:tienda|tienda\\s+de\\s+aplicaciones|software|descubrir|discover)$"), QRegularExpression::CaseInsensitiveOption);
    if (clean.contains(discoverRegex)) {
        if (launchedNameOut) *launchedNameOut = QStringLiteral("Discover");
        return QProcess::startDetached(QStringLiteral("plasma-discover"), QStringList());
    }

    // 9. Semantic alias: Screenshot Tool
    static const QRegularExpression shotRegex(QStringLiteral("^(?:captura|captura\\s+de\\s+pantalla|pantallazo|screenshot|spectacle)$"), QRegularExpression::CaseInsensitiveOption);
    if (clean.contains(shotRegex)) {
        if (launchedNameOut) *launchedNameOut = QStringLiteral("Spectacle");
        return QProcess::startDetached(QStringLiteral("spectacle"), QStringList());
    }

    // 10. Search installed applications via KService
    const auto services = KService::allServices();
    KService::Ptr bestMatch;

    for (const auto &svc : services) {
        if (svc->noDisplay()) continue;
        const QString sName = normalizeForMatch(svc->name());
        const QString sEntry = normalizeForMatch(svc->desktopEntryName());
        if (sName == clean || sEntry == clean) {
            bestMatch = svc;
            break;
        }
    }

    if (!bestMatch) {
        for (const auto &svc : services) {
            if (svc->noDisplay()) continue;
            const QString sName = normalizeForMatch(svc->name());
            const QString sGeneric = normalizeForMatch(svc->genericName());
            const QString sExec = normalizeForMatch(svc->exec());
            if (sName.contains(clean) || sGeneric.contains(clean) || sExec.contains(clean)) {
                bestMatch = svc;
                break;
            }
        }
    }

    if (bestMatch) {
        if (launchedNameOut) *launchedNameOut = bestMatch->name();

        const QString entryName = bestMatch->desktopEntryName();
        if (!entryName.isEmpty()) {
            if (QProcess::startDetached(QStringLiteral("gtk-launch"), QStringList() << entryName)) {
                return true;
            }
        }

        QString cleanExec = bestMatch->exec();
        cleanExec.remove(QRegularExpression(QStringLiteral("%[a-zA-Z]")));
        cleanExec = cleanExec.trimmed();
        QStringList parts = QProcess::splitCommand(cleanExec);
        if (!parts.isEmpty()) {
            const QString prog = parts.takeFirst();
            return QProcess::startDetached(prog, parts);
        }
    }

    // Fallback: execute clean command directly
    QStringList directParts = QProcess::splitCommand(clean);
    if (!directParts.isEmpty()) {
        const QString prog = directParts.takeFirst();
        if (QProcess::startDetached(prog, directParts)) {
            if (launchedNameOut) *launchedNameOut = prog;
            return true;
        }
    }

    return false;
}

bool KiriApplet::closeApplication(const QString &appQuery, QString *closedNameOut)
{
    QString query = appQuery.trimmed();
    query.remove(QRegularExpression(QStringLiteral("[.!?,;¿¡]+$")));
    query = query.trimmed();

    QString clean = normalizeForMatch(query);
    clean.remove(QRegularExpression(QStringLiteral("^(?:el|la|los|las|un|una|the|a|an)\\s+")));
    clean = clean.trimmed();

    if (clean.isEmpty()) return false;

    QString targetProc = clean;

    if (clean.contains(QRegularExpression(QStringLiteral("^(?:terminal|consola|kterminal|konsole)$")))) {
        targetProc = QStringLiteral("konsole");
        if (closedNameOut) *closedNameOut = QStringLiteral("la terminal");
    } else if (clean.contains(QRegularExpression(QStringLiteral("^(?:navegador|internet|browser)$")))) {
        targetProc = QStringLiteral("brave");
        if (closedNameOut) *closedNameOut = QStringLiteral("el navegador");
        QProcess::execute(QStringLiteral("pkill"), QStringList() << QStringLiteral("-f") << QStringLiteral("brave-origin"));
        QProcess::execute(QStringLiteral("pkill"), QStringList() << QStringLiteral("-f") << QStringLiteral("firefox"));
    } else if (clean.contains(QRegularExpression(QStringLiteral("^(?:archivos|dolphin)$")))) {
        targetProc = QStringLiteral("dolphin");
        if (closedNameOut) *closedNameOut = QStringLiteral("Dolphin");
    } else {
        if (closedNameOut) *closedNameOut = query;
    }

    return QProcess::startDetached(QStringLiteral("pkill"), QStringList() << QStringLiteral("-f") << targetProc);
}

QString KiriApplet::extractExecBinaryName(const QString &execLine) const
{
    QString bin = execLine.section(QLatin1Char(' '), 0, 0).trimmed();
    if (bin.contains(QLatin1Char('/'))) {
        bin = QFileInfo(bin).fileName();
    }
    return bin;
}

QStringList KiriApplet::extractTerminalCommands(const QString &markdown) const
{
    QStringList commands;
    static const QRegularExpression codeRegex(QStringLiteral("```(?:bash|sh|zsh)?\\s*\\n([\\s\\S]*?)\\n```"));
    auto it = codeRegex.globalMatch(markdown);
    while (it.hasNext()) {
        auto m = it.next();
        commands.append(m.captured(1).trimmed());
    }
    return commands;
}

void KiriApplet::executeCommandInKonsole(const QString &command)
{
    const QString konsole = QStandardPaths::findExecutable(QStringLiteral("konsole"));
    if (!konsole.isEmpty()) {
        QProcess::startDetached(konsole, QStringList() << QStringLiteral("-e") << QStringLiteral("bash") << QStringLiteral("-c") << QStringLiteral("%1; exec bash").arg(command));
    }
}

void KiriApplet::copyToClipboard(const QString &text) const
{
    if (auto clip = QGuiApplication::clipboard()) {
        clip->setText(text);
    }
}

QString KiriApplet::renderMarkdown(const QString &markdown) const
{
    return markdown;
}

void KiriApplet::openKnowledgeFolder() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kiri/knowledge");
    QDir().mkpath(path);
    QProcess::startDetached(QStringLiteral("dolphin"), QStringList() << path);
}

int KiriApplet::knowledgeDocumentCount() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kiri/knowledge");
    QDir dir(path);
    return dir.entryList(QStringList() << QStringLiteral("*.txt") << QStringLiteral("*.md") << QStringLiteral("*.pdf"), QDir::Files).count();
}

// =========================================================================
// Context Providers (Vitals, Weather, Local Knowledge, Vision)
// =========================================================================

QString KiriApplet::collectSystemVitals() const
{
    QStringList vitals;
    vitals << QStringLiteral("OS: CachyOS Linux (KDE Plasma 6, Wayland)");
    const QString pProfile = getActivePowerProfile();
    if (!pProfile.isEmpty()) {
        vitals << QStringLiteral("Power: %1").arg(pProfile);
    }
    return vitals.join(QStringLiteral(", "));
}

QString KiriApplet::getActivePowerProfile() const
{
    QDBusInterface iface(QStringLiteral("net.hadess.PowerProfiles"),
                         QStringLiteral("/net/hadess/PowerProfiles"),
                         QStringLiteral("org.freedesktop.DBus.Properties"),
                         QDBusConnection::systemBus());
    if (iface.isValid()) {
        QDBusReply<QVariant> reply = iface.call(QStringLiteral("Get"), QStringLiteral("net.hadess.PowerProfiles"), QStringLiteral("ActiveProfile"));
        if (reply.isValid()) {
            return reply.value().toString();
        }
    }
    return QString();
}

bool KiriApplet::setActivePowerProfile(const QString &profile) const
{
    QDBusInterface iface(QStringLiteral("net.hadess.PowerProfiles"),
                         QStringLiteral("/net/hadess/PowerProfiles"),
                         QStringLiteral("org.freedesktop.DBus.Properties"),
                         QDBusConnection::systemBus());
    if (iface.isValid()) {
        QDBusReply<void> reply = iface.call(QStringLiteral("Set"),
                                            QStringLiteral("net.hadess.PowerProfiles"),
                                            QStringLiteral("ActiveProfile"),
                                            QVariant::fromValue(QDBusVariant(profile)));
        return reply.isValid();
    }
    return false;
}

QString KiriApplet::getLocalBatteryInfo() const
{
    QDBusInterface iface(QStringLiteral("org.freedesktop.UPower"),
                         QStringLiteral("/org/freedesktop/UPower/devices/DisplayDevice"),
                         QStringLiteral("org.freedesktop.DBus.Properties"),
                         QDBusConnection::systemBus());
    if (iface.isValid()) {
        QDBusReply<QVariant> pctReply = iface.call(QStringLiteral("Get"), QStringLiteral("org.freedesktop.UPower.Device"), QStringLiteral("Percentage"));
        QDBusReply<QVariant> stateReply = iface.call(QStringLiteral("Get"), QStringLiteral("org.freedesktop.UPower.Device"), QStringLiteral("State"));
        if (pctReply.isValid()) {
            const double pct = pctReply.value().toDouble();
            const int stateVal = stateReply.isValid() ? stateReply.value().toInt() : 0;
            const QString stateStr = (stateVal == 1) ? i18n("cargando") : (stateVal == 2) ? i18n("descargando") : i18n("completa");
            return i18n("La batería está al %1% (%2).", QString::number(pct, 'f', 0), stateStr);
        }
    }
    return QString();
}

bool KiriApplet::isDoNotDisturbActive() const
{
    QDBusInterface iface(QStringLiteral("org.freedesktop.Notifications"),
                         QStringLiteral("/org/freedesktop/Notifications"),
                         QStringLiteral("org.freedesktop.Notifications"),
                         QDBusConnection::sessionBus());
    if (iface.isValid()) {
        QDBusReply<bool> reply = iface.call(QStringLiteral("inhibited"));
        return reply.isValid() ? reply.value() : false;
    }
    return false;
}

bool KiriApplet::setDoNotDisturb(bool active) const
{
    QDBusInterface iface(QStringLiteral("org.freedesktop.Notifications"),
                         QStringLiteral("/org/freedesktop/Notifications"),
                         QStringLiteral("org.freedesktop.Notifications"),
                         QDBusConnection::sessionBus());
    if (iface.isValid()) {
        iface.call(QStringLiteral("Inhibit"), QStringLiteral("Kiri"), active ? QStringLiteral("Modo No Molestar") : QStringLiteral(""));
        return true;
    }
    return false;
}

QString KiriApplet::fetchWeatherContext(const QString &query) const
{
    static const QRegularExpression weatherRegex(QStringLiteral("(tiempo|clima|temperatura|llueve|pron[oó]stico)"), QRegularExpression::CaseInsensitiveOption);
    if (!query.contains(weatherRegex)) return QString();

    // Query wttr.in format=3 for concise one-line weather
    const QUrl url(QStringLiteral("https://wttr.in/?format=3"));
    const QByteArray resp = httpGetSync(url, 2500);
    if (!resp.isEmpty()) {
        return QStringLiteral("Local Weather: %1").arg(QString::fromUtf8(resp).trimmed());
    }
    return QString();
}

QString KiriApplet::fetchDynamicLocalContext(const QString &query, const QString &intent) const
{
    Q_UNUSED(intent)
    static const QRegularExpression pkgRegex(QStringLiteral("(instalar|paquete|versi[oó]n de|cachyos)"), QRegularExpression::CaseInsensitiveOption);
    if (query.contains(pkgRegex)) {
        return QStringLiteral("Linux Distribution: CachyOS (Arch-based rolling release, pacman package manager, Linux CachyOS Kernel).");
    }
    return QString();
}

void KiriApplet::ensureKnowledgeBaseReady()
{
    const QString kbPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kiri/knowledge");
    QDir dir(kbPath);
    if (!dir.exists()) {
        dir.mkpath(kbPath);
    }
}

QString KiriApplet::fetchKnowledgeBaseContext(const QString &query) const
{
    const QString kbDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kiri/knowledge");
    QDir dir(kbDir);
    if (!dir.exists()) {
        return QString();
    }

    const int maxSnippets = readConfigInt(QStringLiteral("knowledgeBaseMaxSnippets"), 3);
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.txt") << QStringLiteral("*.md"), QDir::Files);
    if (files.isEmpty()) {
        return QString();
    }

    // Common stopwords in Spanish and English to extract meaningful search keywords
    static const QSet<QString> stopWords = {
        QStringLiteral("el"), QStringLiteral("la"), QStringLiteral("los"), QStringLiteral("las"),
        QStringLiteral("un"), QStringLiteral("una"), QStringLiteral("unos"), QStringLiteral("unas"),
        QStringLiteral("de"), QStringLiteral("del"), QStringLiteral("en"), QStringLiteral("por"),
        QStringLiteral("para"), QStringLiteral("con"), QStringLiteral("sin"), QStringLiteral("sobre"),
        QStringLiteral("que"), QStringLiteral("qué"), QStringLiteral("como"), QStringLiteral("cómo"),
        QStringLiteral("cual"), QStringLiteral("cuál"), QStringLiteral("donde"), QStringLiteral("dónde"),
        QStringLiteral("cuando"), QStringLiteral("cuándo"), QStringLiteral("quien"), QStringLiteral("quién"),
        QStringLiteral("dime"), QStringLiteral("busca"), QStringLiteral("saber"), QStringLiteral("tengo"),
        QStringLiteral("este"), QStringLiteral("esta"), QStringLiteral("estos"), QStringLiteral("estas"),
        QStringLiteral("the"), QStringLiteral("and"), QStringLiteral("for"), QStringLiteral("with")
    };

    const QStringList tokens = query.toLower().split(QRegularExpression(QStringLiteral("[\\s,;:.!?¡¿]+")), Qt::SkipEmptyParts);
    QStringList keywords;
    for (const QString &tok : tokens) {
        if (tok.length() >= 3 && !stopWords.contains(tok)) {
            keywords.append(tok);
        }
    }

    if (keywords.isEmpty()) {
        return QString();
    }

    struct MatchItem {
        QString filename;
        QString snippet;
        int score = 0;
    };

    QList<MatchItem> matches;

    for (const QString &fname : files) {
        if (fname.startsWith(QStringLiteral("README"), Qt::CaseInsensitive)) {
            continue;
        }

        QFile f(dir.filePath(fname));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString content = QString::fromUtf8(f.readAll());
            int score = 0;
            for (const QString &kw : keywords) {
                if (content.contains(kw, Qt::CaseInsensitive)) {
                    score += 2;
                }
                if (fname.contains(kw, Qt::CaseInsensitive)) {
                    score += 3;
                }
            }

            if (score > 0) {
                MatchItem item;
                item.filename = fname;
                item.score = score;
                item.snippet = content.left(1200);
                matches.append(item);
            }
        }
    }

    if (matches.isEmpty()) {
        return QString();
    }

    std::sort(matches.begin(), matches.end(), [](const MatchItem &a, const MatchItem &b) {
        return a.score > b.score;
    });

    QString accumulated;
    accumulated.append(QStringLiteral("[Local Knowledge Documents (RAG):]\n"));
    const int count = qMin(matches.size(), maxSnippets);
    for (int i = 0; i < count; ++i) {
        accumulated.append(QStringLiteral("--- File: %1 ---\n%2\n\n").arg(matches.at(i).filename, matches.at(i).snippet.trimmed()));
    }

    return accumulated.trimmed();
}

QString KiriApplet::captureScreenToPng(const QString &destPath) const
{
    const QString grim = QStandardPaths::findExecutable(QStringLiteral("grim"));
    if (!grim.isEmpty()) {
        QProcess::execute(grim, QStringList() << destPath);
        return destPath;
    }
    return QString();
}

QString KiriApplet::encodeImageToBase64(const QString &imagePath, int maxDimension) const
{
    QImage img(imagePath);
    if (img.isNull()) return QString();

    if (img.width() > maxDimension || img.height() > maxDimension) {
        img = img.scaled(maxDimension, maxDimension, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "JPEG", 85);
    return QString::fromLatin1(bytes.toBase64());
}

// =========================================================================
// Storage & History Persistence
// =========================================================================

QString KiriApplet::historyFilePath() const
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kiri");
    QDir().mkpath(dataDir);
    return dataDir + QStringLiteral("/history.json");
}

QString KiriApplet::contactsFilePath() const
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kiri");
    QDir().mkpath(dataDir);
    return dataDir + QStringLiteral("/contacts.json");
}

QString KiriApplet::scheduledRemindersFilePath() const
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kiri");
    QDir().mkpath(dataDir);
    return dataDir + QStringLiteral("/reminders.json");
}

void KiriApplet::loadConversationHistory()
{
    QFile f(historyFilePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        m_messages = doc.array().toVariantList();
        Q_EMIT messagesChanged();
    }
}

void KiriApplet::saveConversationHistory()
{
    QFile f(historyFilePath());
    if (f.open(QIODevice::WriteOnly)) {
        const QJsonArray arr = QJsonArray::fromVariantList(m_messages);
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    }
}

void KiriApplet::loadContacts()
{
    QFile f(contactsFilePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        const QJsonArray arr = doc.array();
        m_contacts.clear();
        for (const auto &v : arr) {
            const QJsonObject o = v.toObject();
            m_contacts.append({
                o.value(QStringLiteral("name")).toString(),
                o.value(QStringLiteral("phone")).toString(),
                o.value(QStringLiteral("email")).toString()
            });
        }
    }
}

void KiriApplet::saveContacts()
{
    QFile f(contactsFilePath());
    if (f.open(QIODevice::WriteOnly)) {
        QJsonArray arr;
        for (const auto &c : m_contacts) {
            QJsonObject o;
            o[QStringLiteral("name")] = c.name;
            o[QStringLiteral("phone")] = c.phone;
            o[QStringLiteral("email")] = c.email;
            arr.append(o);
        }
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    }
}

void KiriApplet::loadScheduledReminders()
{
    QFile f(scheduledRemindersFilePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        const QJsonArray arr = doc.array();
        m_scheduledReminders.clear();
        for (const auto &v : arr) {
            const QJsonObject o = v.toObject();
            m_scheduledReminders.append({
                o.value(QStringLiteral("id")).toString(),
                o.value(QStringLiteral("text")).toString(),
                QDateTime::fromString(o.value(QStringLiteral("targetTime")).toString(), Qt::ISODate),
                o.value(QStringLiteral("notified")).toBool()
            });
        }
    }
}

void KiriApplet::saveScheduledReminders()
{
    QFile f(scheduledRemindersFilePath());
    if (f.open(QIODevice::WriteOnly)) {
        QJsonArray arr;
        for (const auto &r : m_scheduledReminders) {
            QJsonObject o;
            o[QStringLiteral("id")] = r.id;
            o[QStringLiteral("text")] = r.text;
            o[QStringLiteral("targetTime")] = r.targetTime.toString(Qt::ISODate);
            o[QStringLiteral("notified")] = r.notified;
            arr.append(o);
        }
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    }
}

void KiriApplet::checkScheduledReminders()
{
    const QDateTime now = QDateTime::currentDateTime();
    bool changed = false;
    for (auto &r : m_scheduledReminders) {
        if (!r.notified && r.targetTime.isValid() && r.targetTime <= now) {
            r.notified = true;
            changed = true;
            triggerReminderNotification(r);
        }
    }
    if (changed) {
        saveScheduledReminders();
    }
}

void KiriApplet::triggerReminderNotification(const ScheduledReminder &reminder)
{
    KNotification::event(QStringLiteral("notification"),
                         i18n("Recordatorio Kiri"),
                         reminder.text);
    speakText(i18n("Recordatorio: %1", reminder.text));
}

// =========================================================================
// WAV Audio & Vision Helpers
// =========================================================================

QByteArray KiriApplet::createWavHeader(int dataSize, int sampleRate, int channels, int bitsPerSample) const
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

QString KiriApplet::detectAvailableVisionModel() const
{
    const QString configured = readConfigString(QStringLiteral("ollamaVisionModel"));
    if (!configured.isEmpty() && m_availableModels.contains(configured)) {
        return configured;
    }

    const QStringList knownVision = {
        QStringLiteral("moondream"),
        QStringLiteral("llava"),
        QStringLiteral("minicpm-v"),
        QStringLiteral("qwen2.5vl"),
        QStringLiteral("llama3.2-vision")
    };

    for (const auto &cand : knownVision) {
        for (const auto &avail : m_availableModels) {
            if (avail.contains(cand, Qt::CaseInsensitive)) {
                return avail;
            }
        }
    }

    return configured.isEmpty() ? QStringLiteral("moondream") : configured;
}

QString KiriApplet::fetchWebSearchContext(const QString &query) const
{
    static const QRegularExpression searchRegex(
        QStringLiteral("(?:busca|buscar|averigua|qui[eé]n es|qu[eé] es)\\s+(.+)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = searchRegex.match(query.trimmed());
    if (!match.hasMatch()) {
        return QString();
    }

    const QString searchTerm = match.captured(1).trimmed();
    if (searchTerm.isEmpty()) {
        return QString();
    }

    QUrl url(QStringLiteral("https://api.duckduckgo.com/"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), searchTerm);
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    q.addQueryItem(QStringLiteral("no_html"), QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("skip_disambig"), QStringLiteral("1"));
    url.setQuery(q);

    const QByteArray resp = httpGetSync(url, 2500);
    if (resp.isEmpty()) {
        return QString();
    }

    const QJsonDocument doc = QJsonDocument::fromJson(resp);
    const QJsonObject obj = doc.object();
    const QString abstractText = obj.value(QStringLiteral("AbstractText")).toString();
    if (!abstractText.isEmpty()) {
        return QStringLiteral("Web Search Summary for \"%1\": %2").arg(searchTerm, abstractText);
    }

    return QString();
}

// =========================================================================
// KDE Connect D-Bus Integration
// =========================================================================

QList<KiriApplet::KdeConnectDevice> KiriApplet::getKdeConnectDevices() const
{
    QList<KdeConnectDevice> result;
    QDBusInterface iface(QStringLiteral("org.kde.kdeconnect"),
                         QStringLiteral("/modules/kdeconnect"),
                         QStringLiteral("org.kde.kdeconnect.daemon"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid()) return result;

    QDBusReply<QStringList> reply = iface.call(QStringLiteral("devices"), true, true);
    if (!reply.isValid()) {
        reply = iface.call(QStringLiteral("devices"));
    }

    if (reply.isValid()) {
        for (const QString &devId : reply.value()) {
            QDBusInterface devIface(QStringLiteral("org.kde.kdeconnect"),
                                    QStringLiteral("/modules/kdeconnect/devices/") + devId,
                                    QStringLiteral("org.kde.kdeconnect.device"),
                                    QDBusConnection::sessionBus());
            QString devName = devId;
            if (devIface.isValid()) {
                const QVariant nameProp = devIface.property("name");
                if (nameProp.isValid() && !nameProp.toString().isEmpty()) {
                    devName = nameProp.toString();
                }
            }
            result.append({devId, devName});
        }
    }
    return result;
}

QString KiriApplet::getKdeConnectBattery(const QString &deviceId) const
{
    QDBusInterface iface(QStringLiteral("org.kde.kdeconnect"),
                         QStringLiteral("/modules/kdeconnect/devices/") + deviceId + QStringLiteral("/battery"),
                         QStringLiteral("org.kde.kdeconnect.device.battery"),
                         QDBusConnection::sessionBus());
    if (iface.isValid()) {
        const QVariant charge = iface.property("charge");
        const QVariant isCharging = iface.property("isCharging");
        if (charge.isValid()) {
            const int pct = charge.toInt();
            const bool charging = isCharging.isValid() ? isCharging.toBool() : false;
            return i18n("La batería de tu teléfono está al %1%%2.", pct, charging ? i18n(" (cargando)") : QString());
        }
    }
    return QString();
}

bool KiriApplet::ringKdeConnectDevice(const QString &deviceId) const
{
    QDBusInterface iface(QStringLiteral("org.kde.kdeconnect"),
                         QStringLiteral("/modules/kdeconnect/devices/") + deviceId + QStringLiteral("/findmyphone"),
                         QStringLiteral("org.kde.kdeconnect.device.findmyphone"),
                         QDBusConnection::sessionBus());
    if (iface.isValid()) {
        QDBusReply<void> reply = iface.call(QStringLiteral("ring"));
        return reply.isValid();
    }
    return false;
}

bool KiriApplet::sendTextToKdeConnect(const QString &deviceId, const QString &phoneNumber, const QString &message) const
{
    if (isRestrictedPhoneNumber(phoneNumber)) return false;

    QDBusInterface iface(QStringLiteral("org.kde.kdeconnect"),
                         QStringLiteral("/modules/kdeconnect/devices/") + deviceId + QStringLiteral("/sms"),
                         QStringLiteral("org.kde.kdeconnect.device.sms"),
                         QDBusConnection::sessionBus());
    if (iface.isValid()) {
        QDBusReply<void> reply = iface.call(QStringLiteral("sendSms"), phoneNumber, message);
        return reply.isValid();
    }
    return false;
}

bool KiriApplet::sendClipboardToKdeConnect(const QString &deviceId, const QString &text) const
{
    QDBusInterface iface(QStringLiteral("org.kde.kdeconnect"),
                         QStringLiteral("/modules/kdeconnect/devices/") + deviceId + QStringLiteral("/clipboard"),
                         QStringLiteral("org.kde.kdeconnect.device.clipboard"),
                         QDBusConnection::sessionBus());
    if (iface.isValid()) {
        QDBusReply<void> reply = iface.call(QStringLiteral("sendClipboardMessage"), text);
        return reply.isValid();
    }
    return false;
}

bool KiriApplet::initiatePhoneCall(const QString &deviceId, const QString &phoneNumber) const
{
    if (isRestrictedPhoneNumber(phoneNumber)) return false;
    const QString cleanNum = sanitizePhoneForDial(phoneNumber);

    QDBusInterface iface(QStringLiteral("org.kde.kdeconnect"),
                         QStringLiteral("/modules/kdeconnect/devices/") + deviceId + QStringLiteral("/telephony"),
                         QStringLiteral("org.kde.kdeconnect.device.telephony"),
                         QDBusConnection::sessionBus());
    if (iface.isValid()) {
        QDBusReply<void> reply = iface.call(QStringLiteral("dial"), cleanNum);
        return reply.isValid();
    }
    return false;
}

QString KiriApplet::normalizePhoneNumber(const QString &raw) const
{
    QString res = raw;
    res.remove(QRegularExpression(QStringLiteral("[^0-9+]")));
    return res;
}

QString KiriApplet::sanitizePhoneForDial(const QString &raw) const
{
    return normalizePhoneNumber(raw);
}

bool KiriApplet::isRestrictedPhoneNumber(const QString &phone) const
{
    const QString clean = normalizePhoneNumber(phone);
    // Block accidental emergency or premium rate dials from automated AI prompts
    return clean == QStringLiteral("112") || clean == QStringLiteral("911") || clean == QStringLiteral("999");
}

// =========================================================================
// Calendar & Email Services
// =========================================================================

KiriApplet::ParsedCalendarEvent KiriApplet::parseCalendarEventPrompt(const QString &prompt, const QDateTime &now) const
{
    ParsedCalendarEvent ev;
    ev.title = prompt.trimmed();
    ev.startTime = now.addSecs(3600); // Default: 1 hour from now
    ev.endTime = ev.startTime.addSecs(3600);

    // Simple time matching: "a las 16:00" or "a las 4 pm"
    static const QRegularExpression timeRegex(QStringLiteral("(?:a las|a la)\\s+(\\d{1,2})(?::(\\d{2}))?\\s*(am|pm)?"), QRegularExpression::CaseInsensitiveOption);
    const auto m = timeRegex.match(prompt);
    if (m.hasMatch()) {
        int hour = m.captured(1).toInt();
        const int minute = m.captured(2).isEmpty() ? 0 : m.captured(2).toInt();
        const QString ampm = m.captured(3).toLower();
        if (ampm == QStringLiteral("pm") && hour < 12) hour += 12;
        if (ampm == QStringLiteral("am") && hour == 12) hour = 0;

        QDate d = now.date();
        if (prompt.contains(QStringLiteral("mañana"), Qt::CaseInsensitive)) {
            d = d.addDays(1);
        }
        ev.startTime = QDateTime(d, QTime(hour, minute));
        ev.endTime = ev.startTime.addSecs(3600);
    }
    return ev;
}

int KiriApplet::monthNameToNumber(const QString &name) const
{
    const QString n = name.toLower();
    if (n.startsWith(QStringLiteral("ene")) || n.startsWith(QStringLiteral("jan"))) return 1;
    if (n.startsWith(QStringLiteral("feb"))) return 2;
    if (n.startsWith(QStringLiteral("mar"))) return 3;
    if (n.startsWith(QStringLiteral("abr")) || n.startsWith(QStringLiteral("apr"))) return 4;
    if (n.startsWith(QStringLiteral("may"))) return 5;
    if (n.startsWith(QStringLiteral("jun"))) return 6;
    if (n.startsWith(QStringLiteral("jul"))) return 7;
    if (n.startsWith(QStringLiteral("ago")) || n.startsWith(QStringLiteral("aug"))) return 8;
    if (n.startsWith(QStringLiteral("sep"))) return 9;
    if (n.startsWith(QStringLiteral("oct"))) return 10;
    if (n.startsWith(QStringLiteral("nov"))) return 11;
    if (n.startsWith(QStringLiteral("dic")) || n.startsWith(QStringLiteral("dec"))) return 12;
    return 1;
}

bool KiriApplet::createCalendarEvent(const QString &title, const QString &datePrompt)
{
    const QString full = title + QStringLiteral(" ") + datePrompt;
    const ParsedCalendarEvent ev = parseCalendarEventPrompt(full, QDateTime::currentDateTime());

    ScheduledReminder r;
    r.id = QStringLiteral("cal_%1").arg(QDateTime::currentMSecsSinceEpoch());
    r.text = ev.title;
    r.targetTime = ev.startTime;
    r.notified = false;
    m_scheduledReminders.append(r);
    saveScheduledReminders();

    openCalendarService(ev);
    return true;
}

void KiriApplet::openCalendarService(const ParsedCalendarEvent &ev) const
{
    const QString client = readConfigString(QStringLiteral("defaultCalendarClient"), QStringLiteral("proton"));
    if (client == QStringLiteral("google")) {
        QDesktopServices::openUrl(buildGoogleCalendarTemplateUrl(ev));
    } else {
        // Proton Calendar or default system calendar
        const QString protonApp = QStandardPaths::findExecutable(QStringLiteral("proton-mail"));
        if (!protonApp.isEmpty()) {
            QProcess::startDetached(protonApp, QStringList());
        } else {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://calendar.proton.me")));
        }
    }
}

QUrl KiriApplet::buildGoogleCalendarTemplateUrl(const ParsedCalendarEvent &ev) const
{
    QUrl url(QStringLiteral("https://calendar.google.com/calendar/render"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("action"), QStringLiteral("TEMPLATE"));
    q.addQueryItem(QStringLiteral("text"), ev.title);
    const QString startFmt = ev.startTime.toUTC().toString(QStringLiteral("yyyyMMdd'T'hhmmss'Z'"));
    const QString endFmt = ev.endTime.toUTC().toString(QStringLiteral("yyyyMMdd'T'hhmmss'Z'"));
    q.addQueryItem(QStringLiteral("dates"), QStringLiteral("%1/%2").arg(startFmt, endFmt));
    if (!ev.description.isEmpty()) {
        q.addQueryItem(QStringLiteral("details"), ev.description);
    }
    url.setQuery(q);
    return url;
}

void KiriApplet::composeEmail(const QString &to, const QString &subject, const QString &body, const QString &service) const
{
    if (service == QStringLiteral("proton")) {
        const QString protonApp = QStandardPaths::findExecutable(QStringLiteral("proton-mail"));
        if (!protonApp.isEmpty()) {
            QProcess::startDetached(protonApp, QStringList());
            return;
        }
    }

    // Default mailto fallback
    QUrl mailtoUrl(QStringLiteral("mailto:") + to);
    QUrlQuery q;
    if (!subject.isEmpty()) q.addQueryItem(QStringLiteral("subject"), subject);
    if (!body.isEmpty()) q.addQueryItem(QStringLiteral("body"), body);
    mailtoUrl.setQuery(q);
    QDesktopServices::openUrl(mailtoUrl);
}

// =========================================================================
// Contacts & Reminders Helpers
// =========================================================================

void KiriApplet::loadReminders()
{
    loadScheduledReminders();
}

void KiriApplet::saveReminders()
{
    saveScheduledReminders();
}

QString KiriApplet::remindersFilePath() const
{
    return scheduledRemindersFilePath();
}

KiriApplet::ContactEntry KiriApplet::findContactByName(const QString &query) const
{
    const QString q = normalizeForMatch(query);
    for (const auto &c : m_contacts) {
        if (normalizeForMatch(c.name).contains(q)) {
            return c;
        }
    }
    return ContactEntry();
}

void KiriApplet::importContactsFromVcf(const QString &vcfPath)
{
    QFile f(vcfPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    ContactEntry cur;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith(QStringLiteral("BEGIN:VCARD"))) {
            cur = ContactEntry();
        } else if (line.startsWith(QStringLiteral("FN:"))) {
            cur.name = line.mid(3).trimmed();
        } else if (line.startsWith(QStringLiteral("TEL;")) || line.startsWith(QStringLiteral("TEL:"))) {
            cur.phone = line.section(QLatin1Char(':'), 1).trimmed();
        } else if (line.startsWith(QStringLiteral("EMAIL;")) || line.startsWith(QStringLiteral("EMAIL:"))) {
            cur.email = line.section(QLatin1Char(':'), 1).trimmed();
        } else if (line.startsWith(QStringLiteral("END:VCARD"))) {
            if (!cur.name.isEmpty()) {
                m_contacts.append(cur);
            }
        }
    }
    saveContacts();
}

QString KiriApplet::normalizeForMatch(const QString &text) const
{
    return text.toLower().normalized(QString::NormalizationForm_D).remove(QRegularExpression(QStringLiteral("[\\p{M}]")));
}

// =========================================================================
// HTTP Synchronous Helpers
// =========================================================================

QByteArray KiriApplet::httpGetSync(const QUrl &url, int timeoutMs) const
{
    QNetworkAccessManager mgr;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KiriApplet/1.0"));

    QNetworkReply *reply = mgr.get(req);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        const QByteArray data = reply->readAll();
        reply->deleteLater();
        return data;
    }
    reply->abort();
    reply->deleteLater();
    return QByteArray();
}

QByteArray KiriApplet::httpPostJsonSync(const QUrl &url, const QJsonObject &payload, int timeoutMs) const
{
    QNetworkAccessManager mgr;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KiriApplet/1.0"));

    QNetworkReply *reply = mgr.post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        const QByteArray data = reply->readAll();
        reply->deleteLater();
        return data;
    }
    reply->abort();
    reply->deleteLater();
    return QByteArray();
}

#include "kiriapplet.moc"
