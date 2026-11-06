/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "kityapplet.h"
#include "mathnotes.h"

#include <QHash>
#include "debug.h"

#include <cmath>

#include <KConfigGroup>
#include <KLocalizedString>
#include <KNotification>
#include <KSharedConfig>
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
#include <QLocale>
#include <QNetworkRequest>
#include <QOperatingSystemVersion>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>
#include <QUrlQuery>

K_PLUGIN_CLASS_WITH_JSON(KityApplet, "metadata.json")

KityApplet::KityApplet(QObject *parent, const KPluginMetaData &data, const QVariantList &args)
    : Plasma::Applet(parent, data, args)
    , m_nam(new QNetworkAccessManager(this))
    , m_vadTimer(new QTimer(this))
    , m_reminderTimer(new QTimer(this))
    , m_readinessTimer(new QTimer(this))
{
}

KityApplet::~KityApplet()
{
    stopRecording();
    stopSpeaking();
    stopWakeWordListener();
}

void KityApplet::init()
{
    Plasma::Applet::init();

    loadConversationHistory();
    loadContacts();
    loadScheduledReminders();
    ensureKnowledgeBaseReady();

    connect(m_vadTimer, &QTimer::timeout, this, &KityApplet::checkVoiceActivity);
    connect(m_reminderTimer, &QTimer::timeout, this, &KityApplet::checkScheduledReminders);
    m_reminderTimer->start(30000); // Check reminders every 30s

    connect(m_readinessTimer, &QTimer::timeout, this, &KityApplet::checkReadiness);
    m_readinessTimer->start(60000); // Check service health every minute

    checkReadiness();
    refreshModels();

    m_wakeWordEnabled = readConfigBool(QStringLiteral("wakeWordEnabled"), true);
    if (m_wakeWordEnabled) {
        startWakeWordListener();
    }

    // Register Kity on session D-Bus for Spotlight / KRunner integration (Phase 4)
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.registerService(QStringLiteral("org.cesar.kity"));
    bus.registerObject(QStringLiteral("/Kity"),
                       this,
                       QDBusConnection::ExportAllInvokables | QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSignals);

    // Wire internal configure action to systemsettings kcm_kity with macOS-style naming
    QAction *cfgAct = internalAction(QStringLiteral("configure"));
    if (cfgAct) {
        cfgAct->disconnect();
        cfgAct->setText(i18n("Ajustes de Kity…"));
        cfgAct->setIcon(QIcon::fromTheme(QStringLiteral("preferences-system")));
        connect(cfgAct, &QAction::triggered, this, &KityApplet::openSettings);
    } else {
        cfgAct = new QAction(QIcon::fromTheme(QStringLiteral("preferences-system")), i18n("Ajustes de Kity…"), this);
        connect(cfgAct, &QAction::triggered, this, &KityApplet::openSettings);
        setInternalAction(QStringLiteral("configure"), cfgAct);
    }
}

void KityApplet::configChanged()
{
    Plasma::Applet::configChanged();

    // 1. Sync wake word listener state with updated configuration
    const bool wakeEnabled = readConfigBool(QStringLiteral("wakeWordEnabled"), true);
    setWakeWordEnabled(wakeEnabled);

    // 2. Re-evaluate models and audio stack readiness
    checkReadiness();
}

// =========================================================================
// Property Accessors
// =========================================================================

QString KityApplet::state() const { return m_state; }
QString KityApplet::statusMessage() const { return m_statusMessage; }
QString KityApplet::streamingText() const { return m_streamingText; }
QVariantList KityApplet::messages() const { return m_messages; }
QStringList KityApplet::availableModels() const { return m_availableModels; }
bool KityApplet::ollamaReachable() const { return m_ollamaReachable; }
bool KityApplet::whisperAvailable() const { return m_whisperAvailable; }
bool KityApplet::piperAvailable() const { return m_piperAvailable; }
bool KityApplet::isRecording() const { return m_state == QLatin1String("listening"); }
bool KityApplet::isBusy() const { return m_state == QLatin1String("thinking") || m_state == QLatin1String("transcribing"); }
bool KityApplet::isSpeaking() const { return m_state == QLatin1String("speaking") || m_isSpeakingSentence; }
qreal KityApplet::audioLevel() const { return m_audioLevel; }
bool KityApplet::wakeWordEnabled() const { return m_wakeWordEnabled; }

void KityApplet::setWakeWordEnabled(bool enabled)
{
    if (m_wakeWordEnabled != enabled) {
        m_wakeWordEnabled = enabled;
        auto kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
        kityrc->group(QStringLiteral("General")).writeEntry(QStringLiteral("wakeWordEnabled"), enabled);
        kityrc->sync();
        KConfigGroup cfg = config();
        cfg.group(QStringLiteral("General")).writeEntry(QStringLiteral("wakeWordEnabled"), enabled);
        cfg.sync();
        Q_EMIT wakeWordEnabledChanged();
        if (m_wakeWordEnabled) {
            startWakeWordListener();
        } else {
            stopWakeWordListener();
        }
    }
}

void KityApplet::setState(const QString &newState, const QString &newStatus)
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
        QTimer::singleShot(450, this, [this]() {
            if (m_state == QLatin1String("idle") && m_wakeWordEnabled) {
                startWakeWordListener();
            }
        });
    } else if (newState == QLatin1String("error") && m_wakeWordEnabled) {
        QTimer::singleShot(3500, this, [this]() {
            if (m_state == QLatin1String("error") && m_wakeWordEnabled) {
                setState(QStringLiteral("idle"), QString());
            }
        });
    }
}

// =========================================================================
// Configuration Readers
// =========================================================================

QString KityApplet::readConfigString(const QString &key, const QString &def) const
{
    const KSharedConfig::Ptr kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    if (kityrc->hasGroup(QStringLiteral("General")) && kityrc->group(QStringLiteral("General")).hasKey(key)) {
        return kityrc->group(QStringLiteral("General")).readEntry(key, def);
    }
    const KConfigGroup cfg = config();
    if (cfg.hasGroup(QStringLiteral("General")) && cfg.group(QStringLiteral("General")).hasKey(key)) {
        return cfg.group(QStringLiteral("General")).readEntry(key, def);
    }
    return cfg.readEntry(key, def);
}

int KityApplet::readConfigInt(const QString &key, int def) const
{
    const KSharedConfig::Ptr kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    if (kityrc->hasGroup(QStringLiteral("General")) && kityrc->group(QStringLiteral("General")).hasKey(key)) {
        return kityrc->group(QStringLiteral("General")).readEntry(key, def);
    }
    const KConfigGroup cfg = config();
    if (cfg.hasGroup(QStringLiteral("General")) && cfg.group(QStringLiteral("General")).hasKey(key)) {
        return cfg.group(QStringLiteral("General")).readEntry(key, def);
    }
    return cfg.readEntry(key, def);
}

double KityApplet::readConfigDouble(const QString &key, double def) const
{
    const KSharedConfig::Ptr kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    if (kityrc->hasGroup(QStringLiteral("General")) && kityrc->group(QStringLiteral("General")).hasKey(key)) {
        return kityrc->group(QStringLiteral("General")).readEntry(key, def);
    }
    const KConfigGroup cfg = config();
    if (cfg.hasGroup(QStringLiteral("General")) && cfg.group(QStringLiteral("General")).hasKey(key)) {
        return cfg.group(QStringLiteral("General")).readEntry(key, def);
    }
    return cfg.readEntry(key, def);
}

bool KityApplet::readConfigBool(const QString &key, bool def) const
{
    const KSharedConfig::Ptr kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    if (kityrc->hasGroup(QStringLiteral("General")) && kityrc->group(QStringLiteral("General")).hasKey(key)) {
        return kityrc->group(QStringLiteral("General")).readEntry(key, def);
    }
    const KConfigGroup cfg = config();
    if (cfg.hasGroup(QStringLiteral("General")) && cfg.group(QStringLiteral("General")).hasKey(key)) {
        return cfg.group(QStringLiteral("General")).readEntry(key, def);
    }
    return cfg.readEntry(key, def);
}

QString KityApplet::resolveOllamaUrl() const
{
    QString url = readConfigString(QStringLiteral("ollamaUrl"), QStringLiteral("http://127.0.0.1:11434"));
    while (url.endsWith(QLatin1Char('/'))) {
        url.chop(1);
    }
    return url;
}

QString KityApplet::resolveOllamaModel() const
{
    return readConfigString(QStringLiteral("ollamaModel"), QStringLiteral("llama3.2:3b"));
}

void KityApplet::setOllamaModel(const QString &model)
{
    if (resolveOllamaModel() != model) {
        auto kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
        kityrc->group(QStringLiteral("General")).writeEntry(QStringLiteral("ollamaModel"), model);
        kityrc->sync();
        KConfigGroup cfg = config();
        cfg.group(QStringLiteral("General")).writeEntry(QStringLiteral("ollamaModel"), model);
        cfg.sync();
        Q_EMIT ollamaModelChanged();
    }
}

QString KityApplet::resolveVisionModel() const
{
    return readConfigString(QStringLiteral("ollamaVisionModel"), resolveOllamaModel());
}

QString KityApplet::resolveSystemPrompt() const
{
    return readConfigString(QStringLiteral("systemPrompt"),
        QStringLiteral("Eres Kity, un asistente de voz e inteligencia artificial nativo para KDE Plasma en CachyOS Linux. "
                       "Responde SIEMPRE en español de España (castellano), de forma concisa, clara, directa y natural, optimizada para síntesis de voz (TTS)."));
}

QString KityApplet::resolveWhisperModelPath() const
{
    QString path = readConfigString(QStringLiteral("whisperModelPath"));
    if (!path.isEmpty() && QFile::exists(path)) {
        return path;
    }
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity/models");
    const QString defaultPath = dataDir + QStringLiteral("/ggml-base.bin");
    if (QFile::exists(defaultPath)) {
        return defaultPath;
    }
    return QString();
}

QString KityApplet::resolveWhisperLanguage() const
{
    const QString lang = readConfigString(QStringLiteral("whisperLanguage"), QStringLiteral("es")).trimmed().toLower();
    if (lang.isEmpty() || lang == QStringLiteral("auto")) {
        return QStringLiteral("es");
    }
    return lang;
}

QString KityApplet::resolvePiperVoicePath() const
{
    QString path = readConfigString(QStringLiteral("piperVoicePath"));
    if (!path.isEmpty() && QFile::exists(path)) {
        return path;
    }
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity/voices");
    const QString defaultVoice = dataDir + QStringLiteral("/es_ES-sharvard-medium.onnx");
    if (QFile::exists(defaultVoice)) {
        return defaultVoice;
    }
    return QString();
}

QString KityApplet::resolveSpeechOutputMode() const
{
    return readConfigString(QStringLiteral("speechOutputMode"), QStringLiteral("always"));
}

void KityApplet::setSpeechOutputMode(const QString &mode)
{
    auto kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    kityrc->group(QStringLiteral("General")).writeEntry(QStringLiteral("speechOutputMode"), mode);
    kityrc->sync();
    KConfigGroup cfg = config();
    cfg.group(QStringLiteral("General")).writeEntry(QStringLiteral("speechOutputMode"), mode);
    cfg.sync();
    Q_EMIT speechOutputModeChanged();
}

bool KityApplet::vadEnabled() const
{
    return readConfigBool(QStringLiteral("vadEnabled"), true);
}

void KityApplet::setVadEnabled(bool enabled)
{
    auto kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    kityrc->group(QStringLiteral("General")).writeEntry(QStringLiteral("vadEnabled"), enabled);
    kityrc->sync();
    KConfigGroup cfg = config();
    cfg.group(QStringLiteral("General")).writeEntry(QStringLiteral("vadEnabled"), enabled);
    cfg.sync();
    Q_EMIT vadEnabledChanged();
}

bool KityApplet::earconsEnabled() const
{
    return readConfigBool(QStringLiteral("earconsEnabled"), true);
}

void KityApplet::setEarconsEnabled(bool enabled)
{
    auto kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    kityrc->group(QStringLiteral("General")).writeEntry(QStringLiteral("earconsEnabled"), enabled);
    kityrc->sync();
    KConfigGroup cfg = config();
    cfg.group(QStringLiteral("General")).writeEntry(QStringLiteral("earconsEnabled"), enabled);
    cfg.sync();
    Q_EMIT earconsEnabledChanged();
}

// =========================================================================
// Health & Readiness
// =========================================================================

void KityApplet::checkReadiness()
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
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KityApplet/1.0"));

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

void KityApplet::refreshModels()
{
    checkReadiness();
}

// =========================================================================
// Conversation & Ollama LLM Interaction
// =========================================================================

void KityApplet::appendAssistantMessage(const QString &text, bool speakIfAppropriate)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    QVariantMap assistantMsg;
    assistantMsg[QStringLiteral("role")] = QStringLiteral("assistant");
    assistantMsg[QStringLiteral("content")] = trimmed;
    assistantMsg[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    m_messages.append(assistantMsg);
    Q_EMIT messagesChanged();
    saveConversationHistory();

    if (speakIfAppropriate) {
        const QString speechMode = resolveSpeechOutputMode();
        if (speechMode == QLatin1String("always") || (speechMode == QLatin1String("voice_only") && m_lastQueryWasVoice)) {
            speakText(trimmed);
            return;
        }
    }

    setState(QStringLiteral("idle"), QString());
}

QString KityApplet::analyzeImageWithVisionModel(const QString &base64Image) const
{
    if (base64Image.isEmpty()) {
        return QString();
    }

    QJsonObject reqPayload;
    reqPayload[QStringLiteral("model")] = detectAvailableVisionModel();
    reqPayload[QStringLiteral("prompt")] = QStringLiteral("Describe accurately and in detail everything visible on this computer screen, including open applications, window titles, visible text, and interface elements.");
    reqPayload[QStringLiteral("stream")] = false;

    QJsonArray imgs;
    imgs.append(base64Image);
    reqPayload[QStringLiteral("images")] = imgs;

    const QUrl genUrl(resolveOllamaUrl() + QStringLiteral("/api/generate"));
    const QByteArray rawResp = httpPostJsonSync(genUrl, reqPayload, 25000);
    if (rawResp.isEmpty()) {
        return QString();
    }

    const QJsonDocument doc = QJsonDocument::fromJson(rawResp);
    return doc.object().value(QStringLiteral("response")).toString().trimmed();
}

void KityApplet::sendMessage(const QString &text, bool fromVoice)
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
    QString effectivePrompt = trimmed;

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

    // 2b. Check clipboard context (Wayland clipboard inspection when user mentions copied text)
    static const QRegularExpression clipRegex(
        QStringLiteral("(portapapeles|clipboard|copiado|he copiado|acabo de copiar|texto copiado)"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(clipRegex)) {
        if (auto clip = QGuiApplication::clipboard()) {
            const QString clipText = clip->text().trimmed();
            if (!clipText.isEmpty()) {
                effectivePrompt = QStringLiteral(
                    "Contenido actual de mi portapapeles del sistema:\n\"\"\"\n%1\n\"\"\"\n\n"
                    "Con base en ese texto copiado en mi portapapeles, responde en español de España a mi petición: %2")
                    .arg(clipText.left(4000), trimmed);
            } else {
                appendAssistantMessage(i18n("Tu portapapeles está vacío en este momento. Copia algún texto primero y vuelve a intentarlo."));
                return;
            }
        }
    }

    // 3. Check vision action (screen vision request)
    QString base64Image;
    static const QRegularExpression screenRegex(
        QStringLiteral("(pantalla|screen|captura|escritorio|mira lo que|analiza(?:r)?\\s+(?:mi\\s+|la\\s+)?pantalla|qu[eé] hay en|qu[eé] ves en|qu[eé]\\s+aplicaci[oó]n\\s+se\\s+muestra)"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(screenRegex)) {
        setState(QStringLiteral("thinking"), i18n("Analizando las ventanas y el contenido de tu pantalla…"));
        QCoreApplication::processEvents();

        // Layer 1: Query KWin Wayland compositor for exact open applications and active windows
        const QString kwinWindows = collectKWinWindowsInfo();

        // Layer 2: Capture screen via Spectacle and extract visible UI text via Tesseract OCR
        const QString tmpShot = QDir::tempPath() + QStringLiteral("/kity_vision_%1.png").arg(QDateTime::currentMSecsSinceEpoch());
        captureScreenToPng(tmpShot);
        const QString ocrText = extractScreenOcrText(tmpShot);
        base64Image = encodeImageToBase64(tmpShot);
        QFile::remove(tmpShot);

        if (kwinWindows.isEmpty() && ocrText.isEmpty() && base64Image.isEmpty()) {
            appendAssistantMessage(i18n("No se pudo inspeccionar la pantalla en este momento."));
            return;
        }

        QStringList screenFacts;
        if (!kwinWindows.isEmpty()) {
            screenFacts << QStringLiteral("1. APLICACIONES Y VENTANAS REALES DETECTADAS EN KWIN WAYLAND:\n%1").arg(kwinWindows);
        }
        if (!ocrText.isEmpty()) {
            screenFacts << QStringLiteral("2. TEXTO VISIBLE EXTRAÍDO DE LA PANTALLA (OCR):\n\"\"\"\n%1\n\"\"\"").arg(ocrText);
        }

        // Only query moondream if neither KWin nor OCR returned meaningful desktop content
        if (screenFacts.isEmpty() && !base64Image.isEmpty()) {
            const QString visualDesc = analyzeImageWithVisionModel(base64Image);
            if (!visualDesc.isEmpty()) {
                screenFacts << QStringLiteral("Descripción visual general:\n%1").arg(visualDesc);
            }
        }
        base64Image.clear();

        effectivePrompt = QStringLiteral(
            "El motor de inspección de escritorio de Kity en KDE Plasma 6.8 (Wayland) acaba de analizar la pantalla actual del usuario y ha obtenido estos datos reales y exactos:\n\n"
            "%1\n\n"
            "Instrucciones estrictas para Kity:\n"
            "- Responde en español de España a la petición del usuario: \"%2\".\n"
            "- Indica con total claridad qué aplicación o aplicaciones se están ejecutando y mostrando en pantalla ahora mismo (por ejemplo, si la ventana activa es Antigravity, menciona explícitamente Antigravity y el título/proyecto que tiene abierto), qué contenido o conversación se lee en la interfaz según el texto extraído, y si hay otras ventanas abiertas o minimizadas.\n"
            "- PROHIBIDO inventar o alucinar objetos que no estén en los datos anteriores (nunca digas que ves un teclado, un ratón, ni digas que no puedes ver la pantalla).")
            .arg(screenFacts.join(QStringLiteral("\n\n")), trimmed);
    }

    sendToOllama(effectivePrompt, context, base64Image);
}

void KityApplet::sendToOllama(const QString &userPrompt, const QString &injectedContext, const QString &base64Image)
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
    if (modelToUse.isEmpty() || modelToUse.contains(QStringLiteral("moondream"), Qt::CaseInsensitive)) {
        modelToUse = resolveOllamaModel();
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

    // Recent history (last 8 messages, filtering out stale vision refusals)
    const int startIdx = qMax(0, m_messages.size() - 8);
    for (int i = startIdx; i < m_messages.size() - 1; ++i) {
        const QVariantMap vm = m_messages.at(i).toMap();
        const QString content = vm.value(QStringLiteral("content")).toString();
        if (content.contains(QStringLiteral("necesitaría que me describieras"), Qt::CaseInsensitive)) {
            continue;
        }
        QJsonObject mObj;
        mObj[QStringLiteral("role")] = vm.value(QStringLiteral("role")).toString();
        mObj[QStringLiteral("content")] = content;
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
    connect(m_currentOllamaReply, &QNetworkReply::readyRead, this, &KityApplet::handleOllamaStreamChunk);
    connect(m_currentOllamaReply, &QNetworkReply::finished, this, &KityApplet::handleOllamaFinished);
}

void KityApplet::handleOllamaStreamChunk()
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

void KityApplet::handleOllamaFinished()
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

void KityApplet::stopGeneration()
{
    if (m_currentOllamaReply) {
        m_currentOllamaReply->abort();
        m_currentOllamaReply->deleteLater();
        m_currentOllamaReply = nullptr;
    }
    stopSpeaking();
    setState(QStringLiteral("idle"), QString());
}

void KityApplet::openUI()
{
    const QString kityBin = QStandardPaths::findExecutable(QStringLiteral("kity"));
    if (!kityBin.isEmpty()) {
        QProcess::startDetached(kityBin, QStringList());
    }
    Q_EMIT openRequested();
}

void KityApplet::toggleUI()
{
    const QString kityBin = QStandardPaths::findExecutable(QStringLiteral("kity"));
    if (!kityBin.isEmpty()) {
        QProcess::startDetached(kityBin, QStringList() << QStringLiteral("-t"));
    }
    Q_EMIT toggleRequested();
}

void KityApplet::openSettings()
{
    QProcess::startDetached(QStringLiteral("systemsettings"), QStringList() << QStringLiteral("kcm_kity"));
}

void KityApplet::reloadConfig()
{
    KSharedConfig::openConfig(QStringLiteral("kityrc"))->reparseConfiguration();
    const bool wakeEnabled = readConfigBool(QStringLiteral("wakeWordEnabled"), true);
    setWakeWordEnabled(wakeEnabled);
    Q_EMIT speechOutputModeChanged();
    Q_EMIT vadEnabledChanged();
    Q_EMIT earconsEnabledChanged();
    Q_EMIT ollamaModelChanged();
    checkReadiness();
    refreshModels();
}

void KityApplet::clearHistory()
{
    m_messages.clear();
    Q_EMIT messagesChanged();
    QFile::remove(historyFilePath());
    setState(QStringLiteral("idle"), QString());
}

// =========================================================================
// Voice & Audio Processing (Whisper.cpp + PipeWire)
// =========================================================================

void KityApplet::startRecording()
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
    playSiriEarcon(true);

    m_currentRecordingPath = QDir::tempPath() + QStringLiteral("/kity_rec_%1.wav").arg(QDateTime::currentMSecsSinceEpoch());

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

    setState(QStringLiteral("listening"), i18n("Escuchando… Habla ahora, Kity detectará cuando termines."));
    startVad();
}

void KityApplet::stopRecording()
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
        playSiriEarcon(false);
        setState(QStringLiteral("transcribing"), i18n("Transcribiendo voz…"));
        runWhisperTranscription();
    }
}

void KityApplet::toggleRecording()
{
    if (m_state == QLatin1String("listening")) {
        stopRecording();
    } else {
        startRecording();
    }
}

void KityApplet::startVad()
{
    m_speechDetected = false;
    m_speechSilenceTimer.invalidate();
    m_vadTimer->start(100); // Check file growth / volume every 100ms
}

void KityApplet::stopVad()
{
    m_vadTimer->stop();
    m_audioLevel = 0.0;
    Q_EMIT audioLevelChanged();
}

void KityApplet::checkVoiceActivity()
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

void KityApplet::runWhisperTranscription()
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

void KityApplet::speakText(const QString &text)
{
    stopSpeaking();
    const QString clean = stripMarkdownForSpeech(text);
    if (!clean.isEmpty()) {
        enqueueSentenceForSpeech(clean);
    }
}

void KityApplet::stopSpeaking()
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

void KityApplet::processStreamingTtsChunk(const QString &newChunk)
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

void KityApplet::enqueueSentenceForSpeech(const QString &sentence)
{
    m_ttsQueue.enqueue(sentence);
    if (!m_isSpeakingSentence) {
        playNextTtsQueueItem();
    }
}

void KityApplet::playNextTtsQueueItem()
{
    if (m_ttsQueue.isEmpty()) {
        m_isSpeakingSentence = false;
        if (m_state == QLatin1String("speaking") || m_state == QLatin1String("transcribing")) {
            setState(QStringLiteral("idle"), QString());
        }
        return;
    }

    const QString sentence = m_ttsQueue.dequeue();
    speakSentence(sentence);
}

void KityApplet::speakSentence(const QString &sentence)
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

QString KityApplet::stripMarkdownForSpeech(const QString &markdown) const
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
// Wake Word Listener ("Hola Kity" / "Oye Kity" / "Hey Kity")
// =========================================================================

void KityApplet::startWakeWordListener()
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
        qWarning() << "Kity: pw-record not found in PATH for wake word listener";
        return;
    }

    m_wakeWordPcmBuffer.clear();
    m_wakeWordEvaluating = false;

    if (!m_wakeWordProcess) {
        m_wakeWordProcess = new QProcess(this);
        connect(m_wakeWordProcess, &QProcess::readyReadStandardOutput, this, &KityApplet::onWakeWordAudioChunkReady);
    }

    QStringList args;
    args << QStringLiteral("--format=s16")
         << QStringLiteral("--rate=16000")
         << QStringLiteral("--channels=1")
         << QStringLiteral("--container=raw")
         << QStringLiteral("-");

    m_wakeWordProcess->start(pwRecord, args);
    if (!m_wakeWordProcess->waitForStarted(2000)) {
        qWarning() << "Kity: Failed to start pw-record for wake word listener";
    } else {
        qInfo() << "Kity: Wake word background listener started successfully (PID:" << m_wakeWordProcess->processId() << ")";
    }
}

void KityApplet::stopWakeWordListener()
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

void KityApplet::onWakeWordAudioChunkReady()
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
            qInfo() << "Kity: Speech detected in background stream (RMS:" << rms << "), capturing wake word window...";

            // Allow 650ms for user to finish saying "Hola Kity"
            QTimer::singleShot(650, this, [this]() {
                if (!m_wakeWordEnabled || m_state != QLatin1String("idle")) {
                    m_wakeWordEvaluating = false;
                    return;
                }
                const QString tmpWav = QDir::tempPath() + QStringLiteral("/kity_ww_probe_%1.wav").arg(QDateTime::currentMSecsSinceEpoch());
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

void KityApplet::checkWakeWordTranscription(const QString &wavPath)
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
         << QStringLiteral("--prompt") << QStringLiteral("Hola Kity, Oye Kity, Hey Kity")
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

        qInfo() << "Kity: Wake word probe transcription result:" << cleaned;

        static const QRegularExpression wwRegex(
            QStringLiteral("(?:hola|oye|hey|ok|quiti|kiti|quity|kitty)?\\s*(?:kity|kitty)|hola\\s*(?:quiti|kiti)"),
            QRegularExpression::CaseInsensitiveOption);

        if (cleaned.contains(wwRegex) || cleaned.contains(QStringLiteral("kity")) || cleaned.contains(QStringLiteral("kitty")) || cleaned.contains(QStringLiteral("quiti")) || cleaned.contains(QStringLiteral("kiti"))) {
            qInfo() << "Kity: WAKE WORD ACTIVATED! ('" << cleaned << "')";

            KNotification::event(QStringLiteral("notification"),
                                 i18n("Kity"),
                                 i18n("¡Hola! Te escucho…"));

            stopWakeWordListener();
            openUI();
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

bool KityApplet::detectAndExecuteSystemAction(const QString &input)
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
            appendAssistantMessage(resp);
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
            appendAssistantMessage(resp);
            return true;
        }
    }

    // 3. Volume Control via PipeWire / WirePlumber (wpctl)
    static const QRegularExpression volUpRegex(QStringLiteral("(?:sube|subir|aumenta|aumentar|sube el|subir el)\\s+(?:volumen|sonido|audio)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(volUpRegex)) {
        QProcess::execute(QStringLiteral("wpctl"), QStringList() << QStringLiteral("set-volume") << QStringLiteral("@DEFAULT_AUDIO_SINK@") << QStringLiteral("5%+"));
        const QString resp = i18n("Subiendo el volumen.");
        appendAssistantMessage(resp);
        return true;
    }

    static const QRegularExpression volDownRegex(QStringLiteral("(?:baja|bajar|disminuye|disminuir|baja el|bajar el)\\s+(?:volumen|sonido|audio)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(volDownRegex)) {
        QProcess::execute(QStringLiteral("wpctl"), QStringList() << QStringLiteral("set-volume") << QStringLiteral("@DEFAULT_AUDIO_SINK@") << QStringLiteral("5%-"));
        const QString resp = i18n("Bajando el volumen.");
        appendAssistantMessage(resp);
        return true;
    }

    static const QRegularExpression volMuteRegex(QStringLiteral("(?:silencia|silenciar|mutea|mutear|quitar)\\s+(?:el\\s+)?(?:volumen|sonido|audio)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(volMuteRegex) || trimmed.compare(QStringLiteral("silencio"), Qt::CaseInsensitive) == 0) {
        QProcess::execute(QStringLiteral("wpctl"), QStringList() << QStringLiteral("set-mute") << QStringLiteral("@DEFAULT_AUDIO_SINK@") << QStringLiteral("1"));
        const QString resp = i18n("Audio silenciado.");
        appendAssistantMessage(resp);
        return true;
    }

    static const QRegularExpression volUnmuteRegex(QStringLiteral("(?:activa|activar|desactiva el silencio|reactiva)\\s+(?:el\\s+)?(?:volumen|sonido|audio)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(volUnmuteRegex)) {
        QProcess::execute(QStringLiteral("wpctl"), QStringList() << QStringLiteral("set-mute") << QStringLiteral("@DEFAULT_AUDIO_SINK@") << QStringLiteral("0"));
        const QString resp = i18n("Audio reactivado.");
        appendAssistantMessage(resp);
        return true;
    }

    // 4. Media Playback Control via playerctl
    static const QRegularExpression mediaPlayRegex(QStringLiteral("(?:reproduce|reanuda|play)\\s+(?:la\\s+)?(?:m[uú]sica|canci[oó]n|audio)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(mediaPlayRegex) || trimmed.compare(QStringLiteral("reproducir"), Qt::CaseInsensitive) == 0) {
        QProcess::execute(QStringLiteral("playerctl"), QStringList() << QStringLiteral("play"));
        const QString resp = i18n("Reanudando reproducción.");
        appendAssistantMessage(resp);
        return true;
    }

    static const QRegularExpression mediaPauseRegex(QStringLiteral("(?:pausa|pausar|para|parar)\\s+(?:la\\s+)?(?:m[uú]sica|canci[oó]n|audio)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(mediaPauseRegex) || trimmed.compare(QStringLiteral("pausa"), Qt::CaseInsensitive) == 0) {
        QProcess::execute(QStringLiteral("playerctl"), QStringList() << QStringLiteral("pause"));
        const QString resp = i18n("Pausando reproducción.");
        appendAssistantMessage(resp);
        return true;
    }

    static const QRegularExpression mediaNextRegex(QStringLiteral("(?:siguiente|pasa\\s+de)\\s+(?:canci[oó]n|pista)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(mediaNextRegex)) {
        QProcess::execute(QStringLiteral("playerctl"), QStringList() << QStringLiteral("next"));
        const QString resp = i18n("Siguiente pista.");
        appendAssistantMessage(resp);
        return true;
    }

    static const QRegularExpression mediaPrevRegex(QStringLiteral("(?:anterior|atr[aá]s)\\s+(?:canci[oó]n|pista)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(mediaPrevRegex)) {
        QProcess::execute(QStringLiteral("playerctl"), QStringList() << QStringLiteral("previous"));
        const QString resp = i18n("Pista anterior.");
        appendAssistantMessage(resp);
        return true;
    }

    // 5. Screen Lock
    static const QRegularExpression lockScreenRegex(QStringLiteral("(?:bloquea|bloquear)\\s+(?:la\\s+)?(?:pantalla|sesi[oó]n|equipo)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(lockScreenRegex)) {
        QProcess::startDetached(QStringLiteral("qdbus6"), QStringList() << QStringLiteral("org.freedesktop.ScreenSaver") << QStringLiteral("/ScreenSaver") << QStringLiteral("Lock"));
        const QString resp = i18n("Bloqueando la pantalla.");
        appendAssistantMessage(resp);
        return true;
    }

    // 6. Screenshot Tool (Spectacle)
    static const QRegularExpression shotActionRegex(QStringLiteral("^(?:haz\\s+una\\s+|toma\\s+una\\s+)?(?:captura(?:\\s+de\\s+pantalla)?|pantallazo)[.!?¿¡]*$"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(shotActionRegex) && !trimmed.contains(QStringLiteral("analiza")) && !trimmed.contains(QStringLiteral("mira"))) {
        QProcess::startDetached(QStringLiteral("spectacle"), QStringList());
        const QString resp = i18n("Abriendo herramienta de captura de pantalla.");
        appendAssistantMessage(resp);
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
            appendAssistantMessage(resp);
            return true;
        }
    }

    // 8. Do Not Disturb: "activa/desactiva no molestar"
    static const QRegularExpression enableDndRegex(QStringLiteral("(?:activa|pon|habilita)\\s+(?:el\\s+modo\\s+)?no\\s+molestar"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(enableDndRegex)) {
        setDoNotDisturb(true);
        const QString resp = i18n("Modo No Molestar activado.");
        appendAssistantMessage(resp);
        return true;
    }

    static const QRegularExpression disableDndRegex(QStringLiteral("(?:desactiva|quita|deshabilita)\\s+(?:el\\s+modo\\s+)?no\\s+molestar"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(disableDndRegex)) {
        setDoNotDisturb(false);
        const QString resp = i18n("Modo No Molestar desactivado.");
        appendAssistantMessage(resp);
        return true;
    }

    // 9. Battery Status: "¿cuánta batería me queda?"
    static const QRegularExpression batteryRegex(QStringLiteral("(bater[ií]a|nivel de carga)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(batteryRegex) && trimmed.contains(QStringLiteral("?"))) {
        const QString bInfo = getLocalBatteryInfo();
        if (!bInfo.isEmpty()) {
            appendAssistantMessage(bInfo);
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
        appendAssistantMessage(resp);
        return true;
    }

    // 11. KDE Connect Find My Phone: "haz sonar mi móvil/teléfono"
    static const QRegularExpression findPhoneRegex(QStringLiteral("(?:haz\\s+sonar|encuentra|busca|d[oó]nde\\s+est[aá])\\s+(?:mi\\s+)?(?:tel[eé]fono|m[oó]vil|dispositivo)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(findPhoneRegex)) {
        const auto devices = getKdeConnectDevices();
        if (!devices.isEmpty()) {
            if (ringKdeConnectDevice(devices.first().id)) {
                const QString resp = i18n("Haciendo sonar tu teléfono (%1)…", devices.first().name);
                appendAssistantMessage(resp);
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
                appendAssistantMessage(bStr);
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
            appendAssistantMessage(resp);
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
        composeEmail(to, i18n("Mensaje de Kity"), body, client);
        const QString resp = i18n("Abriendo cliente de correo para enviar mensaje a %1.", to);
        appendAssistantMessage(resp);
        return true;
    }

    // 15. Exact Volume Percentage: "pon el volumen al 50%" / "volumen al 80 por ciento"
    static const QRegularExpression volExactRegex(
        QStringLiteral("(?:pon\\s+el\\s+|ajusta\\s+el\\s+)?(?:volumen|sonido|audio)\\s+(?:a|al)\\s*(\\d{1,3})\\s*(?:%|por\\s+ciento)?"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch volExactMatch = volExactRegex.match(trimmed);
    if (volExactMatch.hasMatch()) {
        const int pct = qBound(0, volExactMatch.captured(1).toInt(), 100);
        QProcess::execute(QStringLiteral("wpctl"),
                          {QStringLiteral("set-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@"), QStringLiteral("%1%").arg(pct)});
        const QString resp = i18n("Volumen ajustado al %1%.", pct);
        appendAssistantMessage(resp);
        return true;
    }

    // 16. KDE Plasma 6 Dark / Light Theme Switching
    static const QRegularExpression darkThemeRegex(
        QStringLiteral("(?:activa|pon|cambia\\s+a|habilita)\\s+(?:el\\s+)?(?:modo|tema)\\s+(?:oscuro|noche|dark)"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(darkThemeRegex)) {
        QProcess::startDetached(QStringLiteral("plasma-apply-colorscheme"), {QStringLiteral("BreezeDark")});
        const QString resp = i18n("Modo oscuro activado.");
        appendAssistantMessage(resp);
        return true;
    }

    static const QRegularExpression lightThemeRegex(
        QStringLiteral("(?:activa|pon|cambia\\s+a|habilita|desactiva\\s+el\\s+modo\\s+oscuro)\\s*(?:el\\s+)?(?:modo|tema)?\\s*(?:claro|d[ií]a|light)?"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(QRegularExpression(QStringLiteral("(?:modo|tema)\\s+(?:claro|d[ií]a|light)|desactiva(?:r)?\\s+(?:el\\s+)?modo\\s+oscuro"), QRegularExpression::CaseInsensitiveOption))) {
        QProcess::startDetached(QStringLiteral("plasma-apply-colorscheme"), {QStringLiteral("BreezeLight")});
        const QString resp = i18n("Modo claro activado.");
        appendAssistantMessage(resp);
        return true;
    }

    // 17. Screen Brightness Control (KDE Solid PowerManagement D-Bus)
    static const QRegularExpression brightExactRegex(
        QStringLiteral("(?:pon\\s+el\\s+|ajusta\\s+el\\s+)?brillo\\s+(?:a|al)\\s*(\\d{1,3})\\s*(?:%|por\\s+ciento)?"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch bExactMatch = brightExactRegex.match(trimmed);
    if (bExactMatch.hasMatch()) {
        const int pct = qBound(5, bExactMatch.captured(1).toInt(), 100);
        if (adjustScreenBrightness(0, pct)) {
            appendAssistantMessage(i18n("Brillo de pantalla ajustado al %1%.", pct));
        } else {
            appendAssistantMessage(i18n("El control de brillo por software no está disponible para este monitor."));
        }
        return true;
    }

    static const QRegularExpression brightUpRegex(
        QStringLiteral("(?:sube|subir|aumenta|aumentar)\\s+(?:el\\s+)?brillo"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(brightUpRegex)) {
        if (adjustScreenBrightness(+15, -1)) {
            appendAssistantMessage(i18n("Subiendo el brillo de la pantalla."));
        } else {
            appendAssistantMessage(i18n("El control de brillo no está soportado en este monitor externo."));
        }
        return true;
    }

    static const QRegularExpression brightDownRegex(
        QStringLiteral("(?:baja|bajar|disminuye|disminuir|reduce)\\s+(?:el\\s+)?brillo"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(brightDownRegex)) {
        if (adjustScreenBrightness(-15, -1)) {
            appendAssistantMessage(i18n("Bajando el brillo de la pantalla."));
        } else {
            appendAssistantMessage(i18n("El control de brillo no está soportado en este monitor externo."));
        }
        return true;
    }

    // 18. Wi-Fi & Bluetooth Connectivity Control
    static const QRegularExpression wifiOnRegex(
        QStringLiteral("(?:activa|activar|enciende|encender|conecta|habilita)\\s+(?:el\\s+|la\\s+)?(?:wifi|wi-fi|red\\s+inal[aá]mbrica)"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(wifiOnRegex)) {
        QProcess::startDetached(QStringLiteral("nmcli"), {QStringLiteral("radio"), QStringLiteral("wifi"), QStringLiteral("on")});
        appendAssistantMessage(i18n("Wi-Fi activado."));
        return true;
    }

    static const QRegularExpression wifiOffRegex(
        QStringLiteral("(?:desactiva|desactivar|apaga|apagar|desconecta|deshabilita)\\s+(?:el\\s+|la\\s+)?(?:wifi|wi-fi|red\\s+inal[aá]mbrica)"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(wifiOffRegex)) {
        QProcess::startDetached(QStringLiteral("nmcli"), {QStringLiteral("radio"), QStringLiteral("wifi"), QStringLiteral("off")});
        appendAssistantMessage(i18n("Wi-Fi desactivado."));
        return true;
    }

    static const QRegularExpression btOnRegex(
        QStringLiteral("(?:activa|activar|enciende|encender|habilita)\\s+(?:el\\s+)?bluetooth"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(btOnRegex)) {
        QProcess::execute(QStringLiteral("rfkill"), {QStringLiteral("unblock"), QStringLiteral("bluetooth")});
        QProcess::startDetached(QStringLiteral("bluetoothctl"), {QStringLiteral("power"), QStringLiteral("on")});
        appendAssistantMessage(i18n("Bluetooth activado."));
        return true;
    }

    static const QRegularExpression btOffRegex(
        QStringLiteral("(?:desactiva|desactivar|apaga|apagar|deshabilita)\\s+(?:el\\s+)?bluetooth"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(btOffRegex)) {
        QProcess::startDetached(QStringLiteral("bluetoothctl"), {QStringLiteral("power"), QStringLiteral("off")});
        QProcess::execute(QStringLiteral("rfkill"), {QStringLiteral("block"), QStringLiteral("bluetooth")});
        appendAssistantMessage(i18n("Bluetooth desactivado."));
        return true;
    }

    // 19. Session & Power Management (Suspend, Reboot, Shutdown, Logout)
    static const QRegularExpression suspendRegex(
        QStringLiteral("^(?:por\\s+favor\\s+)?(?:suspende|suspender|pon\\s+en\\s+suspensi[oó]n)\\s*(?:el\\s+equipo|el\\s+ordenador|el\\s+pc|el\\s+sistema)?[.!?¿¡]*$"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(suspendRegex)) {
        appendAssistantMessage(i18n("Suspendiendo el equipo…"));
        QTimer::singleShot(800, this, []() {
            QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("suspend")});
        });
        return true;
    }

    static const QRegularExpression rebootRegex(
        QStringLiteral("^(?:por\\s+favor\\s+)?(?:reinicia|reiniciar)\\s+(?:el\\s+equipo|el\\s+ordenador|el\\s+pc|el\\s+sistema)[.!?¿¡]*$"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(rebootRegex)) {
        QProcess::startDetached(QStringLiteral("qdbus6"),
                                {QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("/LogoutPrompt"), QStringLiteral("promptReboot")});
        appendAssistantMessage(i18n("Mostrando confirmación de reinicio del sistema."));
        return true;
    }

    static const QRegularExpression shutdownRegex(
        QStringLiteral("^(?:por\\s+favor\\s+)?(?:apaga|apagar)\\s+(?:el\\s+equipo|el\\s+ordenador|el\\s+pc|el\\s+sistema)[.!?¿¡]*$"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(shutdownRegex)) {
        QProcess::startDetached(QStringLiteral("qdbus6"),
                                {QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("/LogoutPrompt"), QStringLiteral("promptShutDown")});
        appendAssistantMessage(i18n("Mostrando confirmación para apagar el equipo."));
        return true;
    }

    static const QRegularExpression logoutRegex(
        QStringLiteral("^(?:por\\s+favor\\s+)?(?:cierra|cerrar)\\s+(?:la\\s+)?sesi[oó]n[.!?¿¡]*$"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(logoutRegex)) {
        QProcess::startDetached(QStringLiteral("qdbus6"),
                                {QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("/LogoutPrompt"), QStringLiteral("promptLogout")});
        appendAssistantMessage(i18n("Mostrando confirmación para cerrar la sesión."));
        return true;
    }

    // 20. MPRIS2 Now Playing Query ("¿qué canción está sonando?" / "¿qué suena?")
    static const QRegularExpression nowPlayingRegex(
        QStringLiteral("(?:qu[eé]\\s+(?:canci[oó]n|m[uú]sica|pista|tema)\\s+(?:est[aá]\\s+sonando|suena|escucho)|qu[eé]\\s+est[aá]\\s+sonando|qu[eé]\\s+estoy\\s+escuchando)"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(nowPlayingRegex)) {
        appendAssistantMessage(getNowPlayingMetadata());
        return true;
    }

    // 21. Zero-Latency Instant Time & Date Queries
    static const QRegularExpression timeQueryRegex(
        QStringLiteral("^[¿¡]*(?:qu[eé]\\s+hora\\s+es|dime\\s+la\\s+hora|qu[eé]\\s+hora\\s+tienes)(?:\\s+ahora)?[.!?¿¡]*$"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(timeQueryRegex)) {
        const QString timeStr = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
        appendAssistantMessage(i18n("Son las %1.", timeStr));
        return true;
    }

    static const QRegularExpression dateQueryRegex(
        QStringLiteral("^[¿¡]*(?:qu[eé]\\s+d[ií]a\\s+es\\s+hoy|a\\s+qu[eé]\\s+(?:d[ií]a|fecha)\\s+estamos|cu[aá]l\\s+es\\s+la\\s+fecha\\s+de\\s+hoy|dime\\s+la\\s+fecha)[.!?¿¡]*$"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(dateQueryRegex)) {
        const QLocale esLocale(QLocale::Spanish, QLocale::Spain);
        const QString dateStr = esLocale.toString(QDate::currentDate(), QStringLiteral("dddd, d 'de' MMMM 'de' yyyy"));
        appendAssistantMessage(i18n("Hoy es %1.", dateStr));
        return true;
    }

    // 22. Zero-Latency System Resource Queries (RAM, Disk, IP, System Status)
    static const QRegularExpression ramQueryRegex(
        QStringLiteral("(?:cu[aá]nta\\s+(?:memoria\\s+)?ram|uso\\s+de\\s+(?:la\\s+)?(?:memoria\\s+)?ram|memoria\\s+libre)"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(ramQueryRegex)) {
        appendAssistantMessage(getSystemResourceSummary(QStringLiteral("ram")));
        return true;
    }

    static const QRegularExpression diskQueryRegex(
        QStringLiteral("(?:cu[aá]nto\\s+espacio\\s+(?:libre\\s+)?(?:tengo|queda)|espacio\\s+en\\s+disco|almacenamiento\\s+disponible)"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(diskQueryRegex)) {
        appendAssistantMessage(getSystemResourceSummary(QStringLiteral("disk")));
        return true;
    }

    static const QRegularExpression ipQueryRegex(
        QStringLiteral("(?:cu[aá]l\\s+es\\s+mi\\s+(?:direcci[oó]n\\s+)?ip|dime\\s+mi\\s+ip|mi\\s+ip\\s+local)"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(ipQueryRegex)) {
        appendAssistantMessage(getSystemResourceSummary(QStringLiteral("ip")));
        return true;
    }

    static const QRegularExpression sysStatusRegex(
        QStringLiteral("^(?:por\\s+favor\\s+)?(?:estado\\s+del\\s+sistema|resumen\\s+del\\s+sistema|diagn[oó]stico\\s+del\\s+sistema)[.!?¿¡]*$"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(sysStatusRegex)) {
        appendAssistantMessage(getSystemResourceSummary(QStringLiteral("all")));
        return true;
    }

    return false;
}

bool KityApplet::launchApplication(const QString &appQuery, QString *launchedNameOut)
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

bool KityApplet::closeApplication(const QString &appQuery, QString *closedNameOut)
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

QString KityApplet::extractExecBinaryName(const QString &execLine) const
{
    QString bin = execLine.section(QLatin1Char(' '), 0, 0).trimmed();
    if (bin.contains(QLatin1Char('/'))) {
        bin = QFileInfo(bin).fileName();
    }
    return bin;
}

QStringList KityApplet::extractTerminalCommands(const QString &markdown) const
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

void KityApplet::executeCommandInKonsole(const QString &command)
{
    const QString konsole = QStandardPaths::findExecutable(QStringLiteral("konsole"));
    if (!konsole.isEmpty()) {
        QProcess::startDetached(konsole, QStringList() << QStringLiteral("-e") << QStringLiteral("bash") << QStringLiteral("-c") << QStringLiteral("%1; exec bash").arg(command));
    }
}

void KityApplet::copyToClipboard(const QString &text) const
{
    if (auto clip = QGuiApplication::clipboard()) {
        clip->setText(text);
    }
}

QString KityApplet::renderMarkdown(const QString &markdown) const
{
    if (markdown.isEmpty()) {
        return QString();
    }

    // 1. Extract fenced code blocks to protect them from inline markdown replacements
    QString working = markdown;
    QStringList codeBlocks;
    static const QRegularExpression fencedCodeRegex(QStringLiteral("```(?:[a-zA-Z0-9_+\\-]*)?\\s*\\n([\\s\\S]*?)```"));
    auto it = fencedCodeRegex.globalMatch(working);
    int offset = 0;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString codeContent = m.captured(1).trimmed().toHtmlEscaped();
        const QString placeholder = QStringLiteral("@@KITY_CODE_BLOCK_%1@@").arg(codeBlocks.size());
        codeBlocks.append(QStringLiteral("<pre style=\"margin-top:4px; margin-bottom:4px;\"><code>%1</code></pre>").arg(codeContent));
        working.replace(m.capturedStart() + offset, m.capturedLength(), placeholder);
        offset += placeholder.length() - m.capturedLength();
    }

    // 2. Escape HTML entities in non-code text
    QString html = working.toHtmlEscaped();

    // 3. Inline code `...`
    static const QRegularExpression inlineCodeRegex(QStringLiteral("`([^`\\n]+)`"));
    html.replace(inlineCodeRegex, QStringLiteral("<code>\\1</code>"));

    // 4. Bold + Italic ***...***
    static const QRegularExpression boldItalicRegex(QStringLiteral("\\*\\*\\*([^\\*\\n]+)\\*\\*\\*"));
    html.replace(boldItalicRegex, QStringLiteral("<b><i>\\1</i></b>"));

    // 5. Bold **...**
    static const QRegularExpression boldRegex(QStringLiteral("\\*\\*([^\\*\\n]+)\\*\\*"));
    html.replace(boldRegex, QStringLiteral("<b>\\1</b>"));

    // 6. Italic *...*
    static const QRegularExpression italicRegex(QStringLiteral("(?:^|(?<=\\s))\\*([^\\*\\n]+)\\*(?=$|\\s|[.,;:!?])"));
    html.replace(italicRegex, QStringLiteral("<i>\\1</i>"));

    // 7. Markdown links [text](url)
    static const QRegularExpression linkRegex(QStringLiteral("\\[([^\\]]+)\\]\\((https?://[^\\)]+)\\)"));
    html.replace(linkRegex, QStringLiteral("<a href=\"\\2\">\\1</a>"));

    // 8. Headings (#, ##, ###) and unordered list items (- or *) line by line
    QStringList lines = html.split(QLatin1Char('\n'));
    static const QRegularExpression headingRegex(QStringLiteral("^\\s*#{1,4}\\s+(.+)$"));
    static const QRegularExpression listRegex(QStringLiteral("^\\s*[\\-\\*]\\s+(.+)$"));

    for (QString &line : lines) {
        const QRegularExpressionMatch hm = headingRegex.match(line);
        if (hm.hasMatch()) {
            line = QStringLiteral("<b>%1</b>").arg(hm.captured(1));
            continue;
        }
        const QRegularExpressionMatch lm = listRegex.match(line);
        if (lm.hasMatch()) {
            line = QStringLiteral("• %1").arg(lm.captured(1));
        }
    }

    html = lines.join(QStringLiteral("<br>"));

    // 9. Restore fenced code blocks
    for (int i = 0; i < codeBlocks.size(); ++i) {
        html.replace(QStringLiteral("@@KITY_CODE_BLOCK_%1@@").arg(i), codeBlocks.at(i));
    }

    return html;
}

void KityApplet::openKnowledgeFolder() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity/knowledge");
    QDir().mkpath(path);
    QProcess::startDetached(QStringLiteral("dolphin"), QStringList() << path);
}

int KityApplet::knowledgeDocumentCount() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity/knowledge");
    QDir dir(path);
    return dir.entryList(QStringList() << QStringLiteral("*.txt") << QStringLiteral("*.md") << QStringLiteral("*.pdf"), QDir::Files).count();
}

// =========================================================================
// Apple Intelligence Writing Tools (Phase 2)
// =========================================================================

QString KityApplet::readWaylandSelectionOrClipboard() const
{
    QClipboard *cb = QGuiApplication::clipboard();
    if (!cb) {
        return QString();
    }
    QString text = cb->text(QClipboard::Selection).trimmed();
    if (text.isEmpty()) {
        text = cb->text(QClipboard::Clipboard).trimmed();
    }
    return text;
}

QString KityApplet::buildWritingToolPrompt(const QString &action, const QString &sourceText)
{
    const QString cleanText = sourceText.trimmed();
    if (cleanText.isEmpty()) {
        return QString();
    }

    QString instruction;
    if (action.startsWith(QStringLiteral("translate:"))) {
        const QString target = languageDisplayName(action.mid(10));
        instruction = QStringLiteral(
            "Actúa como la app Traducir de Apple. Traduce el siguiente texto a %1 de forma natural y fiel, "
            "detectando automáticamente el idioma de origen. Devuelve únicamente la traducción, sin comentarios.").arg(target);
    } else if (action == QStringLiteral("proofread")) {
        instruction = QStringLiteral(
            "Actúa como la herramienta 'Corregir' (Proofread) de Apple Intelligence. "
            "Corrige la ortografía, gramática y puntuación del siguiente texto en español de España, manteniendo su significado y estilo original. "
            "Devuelve el texto corregido listo para copiar y, debajo, una breve nota de 1 o 2 líneas con las mejoras aplicadas.");
    } else if (action == QStringLiteral("rewrite")) {
        instruction = QStringLiteral(
            "Actúa como la herramienta 'Reescribir' (Rewrite) de Apple Intelligence. "
            "Reescribe el siguiente texto mejorando su fluidez, claridad y expresión en español de España. "
            "Devuelve únicamente la versión reescrita lista para copiar, sin preámbulos.");
    } else if (action == QStringLiteral("friendly")) {
        instruction = QStringLiteral(
            "Reescribe el siguiente texto con un tono cercano, cálido y amigable (Friendly tone) en español de España. "
            "Devuelve únicamente el texto resultante listo para usar.");
    } else if (action == QStringLiteral("professional")) {
        instruction = QStringLiteral(
            "Reescribe el siguiente texto con un tono formal, ejecutivo y profesional (Professional tone) en español de España. "
            "Devuelve únicamente el texto resultante listo para usar.");
    } else if (action == QStringLiteral("concise")) {
        instruction = QStringLiteral(
            "Reescribe el siguiente texto de forma concisa, directa y breve (Concise tone), eliminando redundancias. "
            "Devuelve únicamente el texto resultante listo para usar.");
    } else if (action == QStringLiteral("summary")) {
        instruction = QStringLiteral(
            "Genera un resumen ejecutivo claro y bien estructurado del siguiente texto en español de España.");
    } else if (action == QStringLiteral("keypoints")) {
        instruction = QStringLiteral(
            "Extrae los puntos clave (Key Points) del siguiente texto en una lista de viñetas (Markdown) clara y directa.");
    } else if (action == QStringLiteral("table")) {
        instruction = QStringLiteral(
            "Organiza y estructura la información del siguiente texto en una tabla Markdown clara y bien formateada.");
    } else {
        instruction = QStringLiteral("Mejora y procesa el siguiente texto en español de España:");
    }

    return QStringLiteral("%1\n\nTexto original:\n\"\"\"\n%2\n\"\"\"").arg(instruction, cleanText.left(6000));
}

void KityApplet::applyWritingTool(const QString &action, const QString &customSourceText)
{
    const QString source = !customSourceText.trimmed().isEmpty()
        ? customSourceText.trimmed()
        : readWaylandSelectionOrClipboard();

    if (source.isEmpty()) {
        appendAssistantMessage(i18n("No hay texto seleccionado ni en el portapapeles ni en la caja de entrada. Selecciona o copia un texto primero para aplicar las Herramientas de Escritura."));
        return;
    }

    QString actionLabel = i18n("Herramienta de Escritura");
    if (action == QStringLiteral("proofread")) actionLabel = i18n("✍️ Corregir texto");
    else if (action == QStringLiteral("rewrite")) actionLabel = i18n("✍️ Reescribir texto");
    else if (action == QStringLiteral("friendly")) actionLabel = i18n("✍️ Tono amigable");
    else if (action == QStringLiteral("professional")) actionLabel = i18n("✍️ Tono profesional");
    else if (action == QStringLiteral("concise")) actionLabel = i18n("✍️ Tono conciso");
    else if (action == QStringLiteral("summary")) actionLabel = i18n("✍️ Resumir");
    else if (action == QStringLiteral("keypoints")) actionLabel = i18n("✍️ Puntos clave");
    else if (action == QStringLiteral("table")) actionLabel = i18n("✍️ Convertir en tabla");
    else if (action.startsWith(QStringLiteral("translate:"))) actionLabel = i18n("🌐 Traducir a %1", languageDisplayName(action.mid(10)));

    const QString preview = source.length() > 120 ? source.left(120) + QStringLiteral("…") : source;
    QVariantMap userMsg;
    userMsg[QStringLiteral("role")] = QStringLiteral("user");
    userMsg[QStringLiteral("content")] = QStringLiteral("%1: «%2»").arg(actionLabel, preview);
    userMsg[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    m_messages.append(userMsg);
    Q_EMIT messagesChanged();
    saveConversationHistory();

    m_lastQueryWasVoice = false;
    const QString fullPrompt = buildWritingToolPrompt(action, source);
    sendToOllama(fullPrompt);
}

QString KityApplet::languageDisplayName(const QString &code)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("es"), QStringLiteral("español de España")}, {QStringLiteral("en"), QStringLiteral("inglés")},
        {QStringLiteral("fr"), QStringLiteral("francés")},           {QStringLiteral("de"), QStringLiteral("alemán")},
        {QStringLiteral("it"), QStringLiteral("italiano")},          {QStringLiteral("pt"), QStringLiteral("portugués")},
        {QStringLiteral("ca"), QStringLiteral("catalán")},           {QStringLiteral("ja"), QStringLiteral("japonés")},
        {QStringLiteral("zh"), QStringLiteral("chino simplificado")},
    };
    const QString c = code.trimmed().toLower();
    return names.value(c, c.isEmpty() ? QStringLiteral("español de España") : c);
}

void KityApplet::translateSelection(const QString &targetLanguage)
{
    applyWritingTool(QStringLiteral("translate:") + (targetLanguage.isEmpty() ? QStringLiteral("es") : targetLanguage));
}

QStringList KityApplet::evaluateMathNotes(const QString &text) const
{
    return MathNotes::evaluateLines(text);
}

// =========================================================================
// Visual Intelligence & Live Text (Phase 3 - Spectacle Wayland + Tesseract OCR)
// =========================================================================

QVariantMap KityApplet::parseLiveTextEntities(const QString &ocrText)
{
    QVariantMap res;
    QStringList urls;
    QStringList emails;
    QStringList phones;

    static const QRegularExpression urlRx(QStringLiteral("https?://[\\w\\-._~:/?#\\[\\]@!$&'()*+,;=%]+"), QRegularExpression::CaseInsensitiveOption);
    auto urlIt = urlRx.globalMatch(ocrText);
    while (urlIt.hasNext()) {
        const QString u = urlIt.next().captured(0);
        if (!urls.contains(u)) urls.append(u);
    }

    static const QRegularExpression emailRx(QStringLiteral("[a-zA-Z0-9._%+\\-]+@[a-zA-Z0-9.\\-]+\\.[a-zA-Z]{2,}"), QRegularExpression::CaseInsensitiveOption);
    auto emailIt = emailRx.globalMatch(ocrText);
    while (emailIt.hasNext()) {
        const QString e = emailIt.next().captured(0);
        if (!emails.contains(e)) emails.append(e);
    }

    static const QRegularExpression phoneRx(QStringLiteral("(?:\\+\\d{1,3}[\\s\\-]?)?(?:\\d[\\s\\-]?){8,11}\\d"));
    auto phoneIt = phoneRx.globalMatch(ocrText);
    while (phoneIt.hasNext()) {
        const QString p = phoneIt.next().captured(0).trimmed();
        if (!phones.contains(p)) phones.append(p);
    }

    res[QStringLiteral("urls")] = urls;
    res[QStringLiteral("emails")] = emails;
    res[QStringLiteral("phones")] = phones;
    return res;
}

void KityApplet::runVisualIntelligence(const QString &captureMode, const QString &action, const QString &customQuestion)
{
    const QString mode = captureMode.isEmpty() ? QStringLiteral("region") : captureMode;
    if (mode == QStringLiteral("region")) {
        setState(QStringLiteral("thinking"), i18n("Selecciona una región en pantalla con el ratón (Spectacle Wayland)…"));
    } else if (mode == QStringLiteral("activewindow")) {
        setState(QStringLiteral("thinking"), i18n("Capturando la ventana activa (Spectacle Wayland)…"));
    } else {
        setState(QStringLiteral("thinking"), i18n("Capturando la pantalla completa (Spectacle Wayland)…"));
    }
    QCoreApplication::processEvents();

    const QString tmpShot = QDir::tempPath() + QStringLiteral("/kity_livetext_%1.png").arg(QDateTime::currentMSecsSinceEpoch());
    const QString savedPath = captureScreenToPng(tmpShot, mode);
    if (savedPath.isEmpty()) {
        setState(QStringLiteral("idle"), QString());
        return;
    }

    const QString ocrText = extractScreenOcrText(savedPath);
    const QString kwinInfo = collectKWinWindowsInfo();
    QFile::remove(savedPath);

    QString modeLabel = (mode == QStringLiteral("region")) ? i18n("Región seleccionada")
                      : (mode == QStringLiteral("activewindow")) ? i18n("Ventana activa")
                      : i18n("Pantalla completa");

    if (action == QStringLiteral("ocr")) {
        // Pure instant Live Text OCR extraction + clipboard copy + entity detection
        if (ocrText.isEmpty()) {
            appendAssistantMessage(i18n("👁️ **Live Text (%1):** No se detectó texto legible en la zona capturada.", modeLabel));
            setState(QStringLiteral("idle"), QString());
            return;
        }

        copyToClipboard(ocrText);
        const QVariantMap entities = parseLiveTextEntities(ocrText);
        const QStringList urls = entities.value(QStringLiteral("urls")).toStringList();
        const QStringList emails = entities.value(QStringLiteral("emails")).toStringList();
        const QStringList phones = entities.value(QStringLiteral("phones")).toStringList();

        QString msg = i18n("👁️ **Live Text extraído (%1) — Copiado al portapapeles:**\n\n```\n%2\n```", modeLabel, ocrText);
        if (!urls.isEmpty() || !emails.isEmpty() || !phones.isEmpty()) {
            msg.append(i18n("\n\n**Datos rápidos detectados:**"));
            for (const QString &u : urls) msg.append(QStringLiteral("\n- 🔗 [%1](%1)").arg(u));
            for (const QString &e : emails) msg.append(QStringLiteral("\n- ✉️ [%1](mailto:%1)").arg(e));
            for (const QString &p : phones) msg.append(QStringLiteral("\n- 📞 `%1`").arg(p));
        }

        appendAssistantMessage(msg, false);
        setState(QStringLiteral("idle"), QString());
        return;
    }

    if (ocrText.isEmpty() && kwinInfo.isEmpty()) {
        appendAssistantMessage(i18n("No se pudo extraer texto ni información de la captura (%1).", modeLabel));
        setState(QStringLiteral("idle"), QString());
        return;
    }

    QString userLabel;
    QString aiInstruction;
    if (action == QStringLiteral("translate")) {
        userLabel = i18n("👁️ Traducir captura (%1)", modeLabel);
        aiInstruction = QStringLiteral(
            "Traduce al español de España (o al inglés si el texto original ya está íntegramente en español) todo el texto extraído de la captura de pantalla. "
            "Muestra la traducción clara y bien estructurada.");
    } else if (action == QStringLiteral("explain")) {
        userLabel = i18n("👁️ Explicar / Analizar captura (%1)", modeLabel);
        if (!customQuestion.trimmed().isEmpty()) {
            aiInstruction = QStringLiteral("Responde en español de España a la siguiente pregunta sobre el contenido capturado en pantalla: \"%1\"").arg(customQuestion.trimmed());
        } else {
            aiInstruction = QStringLiteral("Explica de forma clara, estructurada y concisa qué muestra este fragmento de pantalla, resumiendo sus puntos principales o resolviendo cualquier error/código que aparezca.");
        }
    } else {
        userLabel = i18n("👁️ Inteligencia Visual (%1)", modeLabel);
        aiInstruction = QStringLiteral("Analiza y resume en español de España el siguiente contenido capturado en pantalla.");
    }

    QVariantMap userMsg;
    userMsg[QStringLiteral("role")] = QStringLiteral("user");
    userMsg[QStringLiteral("content")] = userLabel;
    userMsg[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    m_messages.append(userMsg);
    Q_EMIT messagesChanged();
    saveConversationHistory();

    m_lastQueryWasVoice = false;
    const QString fullPrompt = QStringLiteral(
        "%1\n\n"
        "Contexto de ventanas KWin Wayland:\n%2\n\n"
        "Texto extraído mediante OCR (Live Text) de la captura (%3):\n\"\"\"\n%4\n\"\"\"")
        .arg(aiInstruction, kwinInfo, modeLabel, ocrText);

    sendToOllama(fullPrompt);
}

// =========================================================================
// Context Providers (Vitals, Weather, Local Knowledge, Vision)
// =========================================================================

QString KityApplet::collectSystemVitals() const
{
    QStringList vitals;
    vitals << QStringLiteral("OS: CachyOS Linux (KDE Plasma 6, Wayland)");
    const QString pProfile = getActivePowerProfile();
    if (!pProfile.isEmpty()) {
        vitals << QStringLiteral("Power: %1").arg(pProfile);
    }
    return vitals.join(QStringLiteral(", "));
}

QString KityApplet::getActivePowerProfile() const
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

bool KityApplet::setActivePowerProfile(const QString &profile) const
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

QString KityApplet::getLocalBatteryInfo() const
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

bool KityApplet::isDoNotDisturbActive() const
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

bool KityApplet::setDoNotDisturb(bool active) const
{
    QDBusInterface iface(QStringLiteral("org.freedesktop.Notifications"),
                         QStringLiteral("/org/freedesktop/Notifications"),
                         QStringLiteral("org.freedesktop.Notifications"),
                         QDBusConnection::sessionBus());
    if (iface.isValid()) {
        iface.call(QStringLiteral("Inhibit"), QStringLiteral("Kity"), active ? QStringLiteral("Modo No Molestar") : QStringLiteral(""));
        return true;
    }
    return false;
}

void KityApplet::playSiriEarcon(bool activating) const
{
    if (!readConfigBool(QStringLiteral("earconsEnabled"), true)) {
        return;
    }

    const QString pwPlay = QStandardPaths::findExecutable(QStringLiteral("pw-play"));
    if (pwPlay.isEmpty()) {
        return;
    }

    const QString earconPath = QDir::tempPath() + (activating ? QStringLiteral("/kity_earcon_on.wav") : QStringLiteral("/kity_earcon_off.wav"));
    if (!QFileInfo::exists(earconPath)) {
        // Synthesize a crystalline dual-tone harmonic Siri earcon (110ms, 16kHz mono PCM)
        const int sampleRate = 16000;
        const int durationSamples = static_cast<int>(sampleRate * 0.11);
        QByteArray pcm(durationSamples * static_cast<int>(sizeof(qint16)), 0);
        qint16 *samples = reinterpret_cast<qint16 *>(pcm.data());

        const double freq1 = activating ? 528.0 : 792.0;
        const double freq2 = activating ? 792.0 : 528.0;
        const double twoPi = 6.283185307179586;

        for (int i = 0; i < durationSamples; ++i) {
            const double progress = static_cast<double>(i) / durationSamples;
            const double freq = (progress < 0.45) ? freq1 : freq2;
            // Smooth Hann window envelope to prevent clicks
            const double envelope = std::sin(3.141592653589793 * progress);
            const double val = std::sin(twoPi * freq * i / sampleRate) * envelope * 0.14;
            samples[i] = static_cast<qint16>(val * 32767.0);
        }

        QFile f(earconPath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(createWavHeader(pcm.size(), sampleRate, 1, 16));
            f.write(pcm);
            f.close();
        }
    }

    if (QFileInfo::exists(earconPath)) {
        QProcess::startDetached(pwPlay, {earconPath});
    }
}

bool KityApplet::adjustScreenBrightness(int deltaPercent, int exactPercent) const
{
    QDBusInterface iface(QStringLiteral("org.kde.Solid.PowerManagement"),
                         QStringLiteral("/org/kde/Solid/PowerManagement/Actions/BrightnessControl"),
                         QStringLiteral("org.kde.Solid.PowerManagement.Actions.BrightnessControl"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid()) {
        return false;
    }

    QDBusReply<int> maxReply = iface.call(QStringLiteral("brightnessMax"));
    QDBusReply<int> curReply = iface.call(QStringLiteral("brightness"));
    if (!maxReply.isValid() || maxReply.value() <= 0) {
        return false;
    }

    const int maxVal = maxReply.value();
    const int curVal = curReply.isValid() ? curReply.value() : (maxVal / 2);
    int targetVal = curVal;

    if (exactPercent >= 0) {
        targetVal = qBound(1, (maxVal * exactPercent) / 100, maxVal);
    } else {
        targetVal = qBound(1, curVal + (maxVal * deltaPercent) / 100, maxVal);
    }

    iface.call(QStringLiteral("setBrightness"), targetVal);
    return true;
}

QString KityApplet::getNowPlayingMetadata() const
{
    const QString playerctl = QStandardPaths::findExecutable(QStringLiteral("playerctl"));
    if (!playerctl.isEmpty()) {
        QProcess proc;
        proc.start(playerctl, {QStringLiteral("metadata"), QStringLiteral("--format"), QStringLiteral("{{title}}|||{{artist}}|||{{album}}")});
        if (proc.waitForFinished(1200) && proc.exitCode() == 0) {
            const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
            if (!out.isEmpty()) {
                const QStringList parts = out.split(QStringLiteral("|||"));
                const QString title = parts.value(0).trimmed();
                const QString artist = parts.value(1).trimmed();
                const QString album = parts.value(2).trimmed();

                if (!title.isEmpty() && !artist.isEmpty()) {
                    if (!album.isEmpty()) {
                        return i18n("Está sonando «%1» de %2 (álbum: %3).", title, artist, album);
                    }
                    return i18n("Está sonando «%1» de %2.", title, artist);
                } else if (!title.isEmpty()) {
                    return i18n("Está reproduciéndose «%1».", title);
                }
            }
        }
    }
    return i18n("No hay ninguna canción ni pista multimedia reproduciéndose en este momento.");
}

QString KityApplet::getSystemResourceSummary(const QString &aspect) const
{
    // Parse /proc/meminfo for RAM
    double totalGiB = 0.0;
    double usedGiB = 0.0;
    double availGiB = 0.0;
    int ramPct = 0;

    QFile memFile(QStringLiteral("/proc/meminfo"));
    if (memFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qint64 memTotalKb = 0;
        qint64 memAvailKb = 0;
        const QString content = QString::fromUtf8(memFile.readAll());
        const QStringList lines = content.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            if (line.startsWith(QStringLiteral("MemTotal:"))) {
                memTotalKb = line.simplified().section(QLatin1Char(' '), 1, 1).toLongLong();
            } else if (line.startsWith(QStringLiteral("MemAvailable:"))) {
                memAvailKb = line.simplified().section(QLatin1Char(' '), 1, 1).toLongLong();
            }
        }
        if (memTotalKb > 0) {
            totalGiB = memTotalKb / (1024.0 * 1024.0);
            availGiB = memAvailKb / (1024.0 * 1024.0);
            usedGiB = totalGiB - availGiB;
            ramPct = qRound((usedGiB / totalGiB) * 100.0);
        }
    }

    if (aspect == QStringLiteral("ram")) {
        return i18n("Estás usando %1 GiB de %2 GiB de memoria RAM (%3% en uso, %4 GiB disponibles).",
                    QString::number(usedGiB, 'f', 1),
                    QString::number(totalGiB, 'f', 1),
                    ramPct,
                    QString::number(availGiB, 'f', 1));
    }

    // Disk info via QStorageInfo
    const QStorageInfo rootStorage = QStorageInfo::root();
    const double diskTotalGiB = rootStorage.bytesTotal() / (1024.0 * 1024.0 * 1024.0);
    const double diskFreeGiB = rootStorage.bytesAvailable() / (1024.0 * 1024.0 * 1024.0);
    const double diskUsedGiB = diskTotalGiB - diskFreeGiB;

    if (aspect == QStringLiteral("disk")) {
        return i18n("Tienes %1 GiB libres de un total de %2 GiB en la partición principal (%3 GiB ocupados).",
                    QString::number(diskFreeGiB, 'f', 1),
                    QString::number(diskTotalGiB, 'f', 1),
                    QString::number(diskUsedGiB, 'f', 1));
    }

    // Local IP address
    QString localIp = i18n("No conectada");
    QProcess ipProc;
    ipProc.start(QStringLiteral("hostname"), {QStringLiteral("-i")});
    if (ipProc.waitForFinished(800) && ipProc.exitCode() == 0) {
        const QString rawIp = QString::fromUtf8(ipProc.readAllStandardOutput()).trimmed();
        if (!rawIp.isEmpty()) {
            localIp = rawIp.section(QLatin1Char(' '), 0, 0);
        }
    }

    if (aspect == QStringLiteral("ip")) {
        return i18n("Tu dirección IP local es %1.", localIp);
    }

    const QString kernel = QSysInfo::kernelVersion();
    const QString distro = QSysInfo::prettyProductName();
    return i18n("**Estado del sistema (%1):**\n"
                "- **Kernel:** Linux `%2`\n"
                "- **Memoria RAM:** %3 GiB / %4 GiB (%5% en uso)\n"
                "- **Disco principal:** %6 GiB libres de %7 GiB\n"
                "- **IP local:** `%8`",
                distro,
                kernel,
                QString::number(usedGiB, 'f', 1),
                QString::number(totalGiB, 'f', 1),
                ramPct,
                QString::number(diskFreeGiB, 'f', 1),
                QString::number(diskTotalGiB, 'f', 1),
                localIp);
}

QString KityApplet::fetchWeatherContext(const QString &query) const
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

QString KityApplet::fetchDynamicLocalContext(const QString &query, const QString &intent) const
{
    Q_UNUSED(intent)
    QStringList ctx;
    static const QRegularExpression pkgRegex(QStringLiteral("(instalar|paquete|versi[oó]n de|cachyos|kernel|sistema|ordenador|pc|hardware|cpu|gpu)"), QRegularExpression::CaseInsensitiveOption);
    if (query.contains(pkgRegex)) {
        ctx << QStringLiteral("Linux Distribution: %1 (Arch-based rolling release, pacman package manager, Kernel %2, KDE Plasma 6.8 on Wayland).")
                   .arg(QSysInfo::prettyProductName(), QSysInfo::kernelVersion());
        ctx << getSystemResourceSummary(QStringLiteral("all"));
    }
    return ctx.join(QStringLiteral("\n"));
}

void KityApplet::ensureKnowledgeBaseReady()
{
    const QString kbPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity/knowledge");
    QDir dir(kbPath);
    if (!dir.exists()) {
        dir.mkpath(kbPath);
    }
}

QString KityApplet::fetchKnowledgeBaseContext(const QString &query) const
{
    const QString kbDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity/knowledge");
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

QString KityApplet::captureScreenToPng(const QString &destPath, const QString &captureMode) const
{
    const QString spectacle = QStandardPaths::findExecutable(QStringLiteral("spectacle"));
    if (!spectacle.isEmpty()) {
        QFile::remove(destPath);
        QString modeFlag = QStringLiteral("-f");
        int timeoutMs = 4500;
        if (captureMode == QStringLiteral("region")) {
            modeFlag = QStringLiteral("-r");
            timeoutMs = 25000; // Allow user time to draw rectangular selection on Wayland
        } else if (captureMode == QStringLiteral("activewindow")) {
            modeFlag = QStringLiteral("-a");
            timeoutMs = 4500;
        }

        QProcess proc;
        proc.start(spectacle, {modeFlag, QStringLiteral("-b"), QStringLiteral("-n"), QStringLiteral("-o"), destPath});

        // Use a non-blocking QEventLoop so plasmashell's D-Bus loop remains responsive
        // and wait until the Spectacle daemon (--dbus) finishes flushing the PNG to disk.
        QEventLoop loop;
        QTimer pollTimer;
        QElapsedTimer deadline;
        deadline.start();

        connect(&proc, &QProcess::finished, &loop, [&](int, QProcess::ExitStatus) {
            if (captureMode == QStringLiteral("region") && !QFileInfo::exists(destPath)) {
                // User cancelled rectangular selection (Escape)
                loop.quit();
            }
        });

        connect(&pollTimer, &QTimer::timeout, &loop, [&]() {
            if (QFileInfo::exists(destPath) && QFileInfo(destPath).size() > 512) {
                QImage testImg(destPath);
                if (!testImg.isNull()) {
                    loop.quit();
                    return;
                }
            }
            if (deadline.elapsed() > timeoutMs) {
                loop.quit();
            }
        });
        pollTimer.start(50);
        loop.exec();

        if (proc.state() != QProcess::NotRunning) {
            proc.waitForFinished(500);
        }

        if (QFileInfo::exists(destPath) && QFileInfo(destPath).size() > 0) {
            return destPath;
        }
    }
    return QString();
}

QString KityApplet::encodeImageToBase64(const QString &imagePath, int maxDimension) const
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

QString KityApplet::collectKWinWindowsInfo() const
{
    const qint64 token = QDateTime::currentMSecsSinceEpoch();
    const QString marker = QStringLiteral("KITY_WIN_%1:").arg(token);
    const QString scriptName = QStringLiteral("kity_win_probe_%1").arg(token);
    const QString scriptPath = QDir::tempPath() + QStringLiteral("/%1.js").arg(scriptName);

    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return QString();
    }

    const QString jsCode = QStringLiteral(
        "const wins = workspace.windowList();\n"
        "for (let i = 0; i < wins.length; ++i) {\n"
        "    const w = wins[i];\n"
        "    if (w.normalWindow && !w.skipTaskbar) {\n"
        "        print(\"%1\" + w.resourceClass + \"|||\" + w.caption + \"|||\" + (w.active ? \"1\" : \"0\") + \"|||\" + (w.minimized ? \"1\" : \"0\"));\n"
        "    }\n"
        "}\n").arg(marker);
    scriptFile.write(jsCode.toUtf8());
    scriptFile.close();

    QDBusInterface scriptingIface(QStringLiteral("org.kde.KWin"),
                                  QStringLiteral("/Scripting"),
                                  QStringLiteral("org.kde.kwin.Scripting"),
                                  QDBusConnection::sessionBus());
    if (!scriptingIface.isValid()) {
        QFile::remove(scriptPath);
        return QString();
    }

    QDBusReply<int> loadReply = scriptingIface.call(QStringLiteral("loadScript"), scriptPath, scriptName);
    if (loadReply.isValid()) {
        const int scriptId = loadReply.value();
        QDBusInterface scriptObj(QStringLiteral("org.kde.KWin"),
                                 QStringLiteral("/Scripting/Script%1").arg(scriptId),
                                 QStringLiteral("org.kde.kwin.Script"),
                                 QDBusConnection::sessionBus());
        if (scriptObj.isValid()) {
            scriptObj.call(QStringLiteral("run"));
        } else {
            scriptingIface.call(QStringLiteral("start"));
        }
        scriptingIface.call(QStringLiteral("unloadScript"), scriptName);
    }
    QFile::remove(scriptPath);

    // Give journald ~80ms to flush kwin_wayland's print() output
    QEventLoop waitLoop;
    QTimer::singleShot(80, &waitLoop, &QEventLoop::quit);
    waitLoop.exec();

    QProcess jProc;
    jProc.start(QStringLiteral("journalctl"),
                {QStringLiteral("--user"), QStringLiteral("-u"), QStringLiteral("plasma-kwin_wayland.service"),
                 QStringLiteral("-n"), QStringLiteral("60"), QStringLiteral("--no-pager")});
    jProc.waitForFinished(1000);
    const QString logOutput = QString::fromUtf8(jProc.readAllStandardOutput());

    QStringList visibleWindows;
    QStringList minimizedWindows;
    QString activeWindowSummary;

    const QStringList lines = logOutput.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int idx = line.indexOf(marker);
        if (idx < 0) continue;

        const QString payload = line.mid(idx + marker.length()).trimmed();
        const QStringList parts = payload.split(QStringLiteral("|||"));
        if (parts.size() < 4) continue;

        const QString appClass = parts.at(0).trimmed();
        const QString caption = parts.at(1).trimmed();
        const bool isActive = (parts.at(2) == QStringLiteral("1"));
        const bool isMinimized = (parts.at(3) == QStringLiteral("1"));

        // Ignore plasmashell itself
        if (appClass.contains(QStringLiteral("plasmashell"), Qt::CaseInsensitive)) {
            continue;
        }

        const QString formatted = QStringLiteral("Aplicación: `%1` — Título de ventana: \"%2\"").arg(appClass, caption);
        if (isMinimized) {
            minimizedWindows << formatted;
        } else {
            if (isActive) {
                activeWindowSummary = formatted + QStringLiteral(" (VENTANA PRINCIPAL ACTIVA Y EN PRIMER PLANO)");
                visibleWindows.prepend(activeWindowSummary);
            } else {
                visibleWindows.append(formatted);
            }
        }
    }

    QStringList result;
    if (!visibleWindows.isEmpty()) {
        result << QStringLiteral("Ventanas visibles actualmente en pantalla:\n- ") + visibleWindows.join(QStringLiteral("\n- "));
    }
    if (!minimizedWindows.isEmpty()) {
        result << QStringLiteral("Ventanas minimizadas en segundo plano (ocultas):\n- ") + minimizedWindows.join(QStringLiteral("\n- "));
    }
    return result.join(QStringLiteral("\n\n"));
}

QString KityApplet::extractScreenOcrText(const QString &imagePath) const
{
    if (imagePath.isEmpty() || !QFileInfo::exists(imagePath)) {
        return QString();
    }

    const QString tesseract = QStandardPaths::findExecutable(QStringLiteral("tesseract"));
    if (tesseract.isEmpty()) {
        return QString();
    }

    QProcess proc;
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(&proc, &QProcess::finished, &loop, &QEventLoop::quit);

    proc.start(tesseract, {imagePath, QStringLiteral("stdout"), QStringLiteral("--psm"), QStringLiteral("3")});
    timeoutTimer.start(3500);
    loop.exec();

    if (proc.state() != QProcess::NotRunning) {
        proc.kill();
        proc.waitForFinished(200);
    }

    const QString rawOcr = QString::fromUtf8(proc.readAllStandardOutput());
    QStringList cleanLines;
    const QStringList lines = rawOcr.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString t = line.trimmed();
        // Keep lines with meaningful text (at least 4 characters and contains letters)
        if (t.length() >= 4 && t.contains(QRegularExpression(QStringLiteral("[a-zA-ZáéíóúÁÉÍÓÚñÑ]{3,}")))) {
            cleanLines << t;
        }
    }

    return cleanLines.join(QLatin1Char('\n')).left(2400).trimmed();
}

// =========================================================================
// Storage & History Persistence
// =========================================================================

QString KityApplet::historyFilePath() const
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity");
    QDir().mkpath(dataDir);
    return dataDir + QStringLiteral("/history.json");
}

QString KityApplet::contactsFilePath() const
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity");
    QDir().mkpath(dataDir);
    return dataDir + QStringLiteral("/contacts.json");
}

QString KityApplet::scheduledRemindersFilePath() const
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity");
    QDir().mkpath(dataDir);
    return dataDir + QStringLiteral("/reminders.json");
}

void KityApplet::loadConversationHistory()
{
    QFile f(historyFilePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        m_messages = doc.array().toVariantList();
        Q_EMIT messagesChanged();
    }
}

void KityApplet::saveConversationHistory()
{
    QFile f(historyFilePath());
    if (f.open(QIODevice::WriteOnly)) {
        const QJsonArray arr = QJsonArray::fromVariantList(m_messages);
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    }
}

void KityApplet::loadContacts()
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

void KityApplet::saveContacts()
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

void KityApplet::loadScheduledReminders()
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

void KityApplet::saveScheduledReminders()
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

void KityApplet::checkScheduledReminders()
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

void KityApplet::triggerReminderNotification(const ScheduledReminder &reminder)
{
    KNotification::event(QStringLiteral("notification"),
                         i18n("Recordatorio Kity"),
                         reminder.text);
    speakText(i18n("Recordatorio: %1", reminder.text));
}

// =========================================================================
// WAV Audio & Vision Helpers
// =========================================================================

QByteArray KityApplet::createWavHeader(int dataSize, int sampleRate, int channels, int bitsPerSample) const
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

QString KityApplet::detectAvailableVisionModel() const
{
    const QString configured = readConfigString(QStringLiteral("ollamaVisionModel"));
    if (!configured.isEmpty() && m_availableModels.contains(configured)) {
        return configured;
    }

    const QStringList knownVision = {
        QStringLiteral("llama3.2-vision"),
        QStringLiteral("llava"),
        QStringLiteral("bakllava"),
        QStringLiteral("minicpm-v"),
        QStringLiteral("qwen2.5vl")
    };

    for (const auto &cand : knownVision) {
        for (const auto &avail : m_availableModels) {
            if (avail.contains(cand, Qt::CaseInsensitive)) {
                return avail;
            }
        }
    }

    return configured.isEmpty() ? resolveOllamaModel() : configured;
}

QString KityApplet::fetchWebSearchContext(const QString &query) const
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

QList<KityApplet::KdeConnectDevice> KityApplet::getKdeConnectDevices() const
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

QString KityApplet::getKdeConnectBattery(const QString &deviceId) const
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

bool KityApplet::ringKdeConnectDevice(const QString &deviceId) const
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

bool KityApplet::sendTextToKdeConnect(const QString &deviceId, const QString &phoneNumber, const QString &message) const
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

bool KityApplet::sendClipboardToKdeConnect(const QString &deviceId, const QString &text) const
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

bool KityApplet::initiatePhoneCall(const QString &deviceId, const QString &phoneNumber) const
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

QString KityApplet::normalizePhoneNumber(const QString &raw) const
{
    QString res = raw;
    res.remove(QRegularExpression(QStringLiteral("[^0-9+]")));
    return res;
}

QString KityApplet::sanitizePhoneForDial(const QString &raw) const
{
    return normalizePhoneNumber(raw);
}

bool KityApplet::isRestrictedPhoneNumber(const QString &phone) const
{
    const QString clean = normalizePhoneNumber(phone);
    // Block accidental emergency or premium rate dials from automated AI prompts
    return clean == QStringLiteral("112") || clean == QStringLiteral("911") || clean == QStringLiteral("999");
}

// =========================================================================
// Calendar & Email Services
// =========================================================================

KityApplet::ParsedCalendarEvent KityApplet::parseCalendarEventPrompt(const QString &prompt, const QDateTime &now) const
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

int KityApplet::monthNameToNumber(const QString &name) const
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

bool KityApplet::createCalendarEvent(const QString &title, const QString &datePrompt)
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

void KityApplet::openCalendarService(const ParsedCalendarEvent &ev) const
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

QUrl KityApplet::buildGoogleCalendarTemplateUrl(const ParsedCalendarEvent &ev) const
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

void KityApplet::composeEmail(const QString &to, const QString &subject, const QString &body, const QString &service) const
{
    if (service == QStringLiteral("proton")) {
        const QString protonApp = QStandardPaths::findExecutable(QStringLiteral("proton-mail"));
        if (!protonApp.isEmpty()) {
            QProcess::startDetached(protonApp, QStringList());
            return;
        }
    } else if (service == QStringLiteral("gmail")) {
        QUrl gmailUrl(QStringLiteral("https://mail.google.com/mail/"));
        QUrlQuery gq;
        gq.addQueryItem(QStringLiteral("view"), QStringLiteral("cm"));
        gq.addQueryItem(QStringLiteral("fs"), QStringLiteral("1"));
        if (!to.isEmpty()) gq.addQueryItem(QStringLiteral("to"), to);
        if (!subject.isEmpty()) gq.addQueryItem(QStringLiteral("su"), subject);
        if (!body.isEmpty()) gq.addQueryItem(QStringLiteral("body"), body);
        gmailUrl.setQuery(gq);
        QDesktopServices::openUrl(gmailUrl);
        return;
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

void KityApplet::loadReminders()
{
    loadScheduledReminders();
}

void KityApplet::saveReminders()
{
    saveScheduledReminders();
}

QString KityApplet::remindersFilePath() const
{
    return scheduledRemindersFilePath();
}

KityApplet::ContactEntry KityApplet::findContactByName(const QString &query) const
{
    const QString q = normalizeForMatch(query);
    for (const auto &c : m_contacts) {
        if (normalizeForMatch(c.name).contains(q)) {
            return c;
        }
    }
    return ContactEntry();
}

void KityApplet::importContactsFromVcf(const QString &vcfPath)
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

QString KityApplet::normalizeForMatch(const QString &text) const
{
    return text.toLower().normalized(QString::NormalizationForm_D).remove(QRegularExpression(QStringLiteral("[\\p{M}]")));
}

// =========================================================================
// HTTP Synchronous Helpers
// =========================================================================

QByteArray KityApplet::httpGetSync(const QUrl &url, int timeoutMs) const
{
    QNetworkAccessManager mgr;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KityApplet/1.0"));

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

QByteArray KityApplet::httpPostJsonSync(const QUrl &url, const QJsonObject &payload, int timeoutMs) const
{
    QNetworkAccessManager mgr;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KityApplet/1.0"));

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

#include "kityapplet.moc"
