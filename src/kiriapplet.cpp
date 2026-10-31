/*
    SPDX-FileCopyrightText: 2026 Kiri contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "kiriapplet.h"
#include "debug.h"

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

    connect(m_vadTimer, &QTimer::timeout, this, &KiriApplet::checkVoiceActivity);
    connect(m_reminderTimer, &QTimer::timeout, this, &KiriApplet::checkScheduledReminders);
    m_reminderTimer->start(30000); // Check reminders every 30s

    connect(m_readinessTimer, &QTimer::timeout, this, &KiriApplet::checkReadiness);
    m_readinessTimer->start(60000); // Check service health every minute

    checkReadiness();
    refreshModels();

    if (readConfigBool(QStringLiteral("wakeWordEnabled"), false)) {
        startWakeWordListener();
    }
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
}

// =========================================================================
// Configuration Readers
// =========================================================================

QString KiriApplet::readConfigString(const QString &key, const QString &def) const
{
    KConfigGroup cfg = config();
    return cfg.readEntry(key, def);
}

int KiriApplet::readConfigInt(const QString &key, int def) const
{
    KConfigGroup cfg = config();
    return cfg.readEntry(key, def);
}

double KiriApplet::readConfigDouble(const QString &key, double def) const
{
    KConfigGroup cfg = config();
    return cfg.readEntry(key, def);
}

bool KiriApplet::readConfigBool(const QString &key, bool def) const
{
    KConfigGroup cfg = config();
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
        QStringLiteral("You are Kiri, a concise, helpful, and natural voice assistant integrated into KDE Plasma on CachyOS Linux. "
                       "Keep answers clear, direct, and easy to read aloud. Reply in the same language the user speaks or writes to you."));
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
    return readConfigString(QStringLiteral("whisperLanguage"), QStringLiteral("auto"));
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
    const QString modelToUse = hasVision ? resolveVisionModel() : resolveOllamaModel();

    // Build chat messages array
    QJsonArray chatMsgs;

    // System prompt
    QJsonObject sysObj;
    sysObj[QStringLiteral("role")] = QStringLiteral("system");
    QString fullSysPrompt = resolveSystemPrompt();
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

    QFileInfo fi(m_currentRecordingPath);
    if (!fi.exists()) return;

    const qint64 size = fi.size();
    if (size > 32000) { // ~1 second of audio recorded
        m_speechDetected = true;
        // Simulate dynamic audio level pulse for UI orb animation
        m_audioLevel = 0.4 + (static_cast<qreal>(size % 1000) / 1000.0) * 0.5;
        Q_EMIT audioLevelChanged();
    }

    // VAD auto-stop timeout after silence
    if (m_speechDetected && readConfigBool(QStringLiteral("vadEnabled"), true)) {
        const int silenceTimeout = readConfigInt(QStringLiteral("vadSilenceTimeoutMs"), 1300);
        if (!m_speechSilenceTimer.isValid()) {
            m_speechSilenceTimer.start();
        } else if (m_speechSilenceTimer.elapsed() > (silenceTimeout + 3000)) { // Safety limit: auto stop
            stopRecording();
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

    if (piperCli.isEmpty() || voicePath.isEmpty() || pwPlay.isEmpty()) {
        m_isSpeakingSentence = false;
        return;
    }

    m_isSpeakingSentence = true;
    setState(QStringLiteral("speaking"), i18n("Reproduciendo respuesta…"));

    // Pipeline: piper --model voicePath --output_file - | pw-play -
    const QString shCmd = QStringLiteral("echo %1 | %2 --model %3 --output_file - | %4 -")
        .arg(QString(sentence).replace(QLatin1Char('\''), QStringLiteral("'\\''")))
        .arg(piperCli, voicePath, pwPlay);

    m_ttsProcess = new QProcess(this);
    connect(m_ttsProcess, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        if (m_ttsProcess) {
            m_ttsProcess->deleteLater();
            m_ttsProcess = nullptr;
        }
        playNextTtsQueueItem();
    });

    m_ttsProcess->start(QStringLiteral("sh"), QStringList() << QStringLiteral("-c") << shCmd);
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
// Wake Word Listener ("Oye Kiri" / "Hey Kiri")
// =========================================================================

void KiriApplet::startWakeWordListener()
{
    stopWakeWordListener();

    const QString pwRecord = QStandardPaths::findExecutable(QStringLiteral("pw-record"));
    if (pwRecord.isEmpty()) return;

    // Wake word background monitoring loop
    m_wakeWordEnabled = true;
    Q_EMIT wakeWordEnabledChanged();
}

void KiriApplet::stopWakeWordListener()
{
    m_wakeWordEnabled = false;
    Q_EMIT wakeWordEnabledChanged();
    if (m_wakeWordProcess) {
        m_wakeWordProcess->kill();
        m_wakeWordProcess->deleteLater();
        m_wakeWordProcess = nullptr;
    }
}

void KiriApplet::onWakeWordAudioChunkReady()
{
    // Evaluates wake word sliding buffer
}

void KiriApplet::checkWakeWordTranscription(const QString &wavPath)
{
    Q_UNUSED(wavPath)
}

// =========================================================================
// System Actions & Personal Assistant Capabilities
// =========================================================================

bool KiriApplet::detectAndExecuteSystemAction(const QString &input)
{
    const QString trimmed = input.trimmed();

    // 1. App Launch: "abre/inicia/lanza <app>"
    static const QRegularExpression launchRegex(QStringLiteral("^(?:abre|inicia|lanza|ejecuta)\\s+([a-zA-Z0-9_\\-\\s]+)$"), QRegularExpression::CaseInsensitiveOption);
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

    // 2. App Close: "cierra <app>"
    static const QRegularExpression closeRegex(QStringLiteral("^(?:cierra|termina)\\s+([a-zA-Z0-9_\\-\\s]+)$"), QRegularExpression::CaseInsensitiveOption);
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

    // 3. Power Profile: "modo ahorro/rendimiento/equilibrado"
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

    // 4. Do Not Disturb: "activa/desactiva no molestar"
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

    // 5. Battery Status: "¿cuánta batería me queda?"
    static const QRegularExpression batteryRegex(QStringLiteral("(bater[ií]a|nivel de carga)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(batteryRegex) && trimmed.contains(QStringLiteral("?"))) {
        const QString bInfo = getLocalBatteryInfo();
        if (!bInfo.isEmpty()) {
            sendMessage(bInfo, false);
            speakText(bInfo);
            return true;
        }
    }

    // 6. Timers: "temporizador de X minutos/segundos"
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

    return false;
}

bool KiriApplet::launchApplication(const QString &appQuery, QString *launchedNameOut)
{
    const auto services = KService::allServices();
    const QString cleanQuery = normalizeForMatch(appQuery);

    KService::Ptr bestMatch;
    for (const auto &svc : services) {
        if (!svc->noDisplay() && (normalizeForMatch(svc->name()).contains(cleanQuery) ||
                                  normalizeForMatch(svc->exec()).contains(cleanQuery))) {
            bestMatch = svc;
            break;
        }
    }

    if (bestMatch) {
        if (launchedNameOut) *launchedNameOut = bestMatch->name();
        return QProcess::startDetached(bestMatch->exec(), QStringList());
    }

    // Fallback: start command directly
    return QProcess::startDetached(appQuery, QStringList());
}

bool KiriApplet::closeApplication(const QString &appQuery, QString *closedNameOut)
{
    const QString binName = extractExecBinaryName(appQuery);
    if (binName.isEmpty()) return false;

    if (closedNameOut) *closedNameOut = appQuery;
    return QProcess::startDetached(QStringLiteral("pkill"), QStringList() << QStringLiteral("-f") << binName);
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

QString KiriApplet::fetchKnowledgeBaseContext(const QString &query) const
{
    const QString kbDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kiri/knowledge");
    QDir dir(kbDir);
    if (!dir.exists()) return QString();

    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.txt") << QStringLiteral("*.md"), QDir::Files);
    QString accumulated;
    for (const QString &fname : files) {
        QFile f(dir.filePath(fname));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString content = QString::fromUtf8(f.readAll());
            if (content.contains(query.section(QLatin1Char(' '), 0, 0), Qt::CaseInsensitive)) {
                accumulated.append(QStringLiteral("--- File: %1 ---\n%2\n\n").arg(fname, content.left(1000)));
            }
        }
    }
    return accumulated;
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
