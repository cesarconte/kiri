/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "kityapplet.h"
#include "mathnotes.h"
#include "mobileserver.h"

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
#include <QDBusMessage>
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
#include <QTextStream>
#include <QSysInfo>
#include <QUrlQuery>
#include <QUuid>

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
    if (m_currentOllamaReply) {
        m_currentOllamaReply->abort();
        m_currentOllamaReply->deleteLater();
        m_currentOllamaReply = nullptr;
    }
    if (m_currentGeminiReply) {
        m_currentGeminiReply->abort();
        m_currentGeminiReply->deleteLater();
        m_currentGeminiReply = nullptr;
    }
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

    // Initialize Kity Mobile Remote Web/WebSocket Server (Apple Intelligence PWA - Ports 1740/1741 in KDE Connect Firewall Range)
    m_mobileServer = new KityMobileServer(1740, this);
    connect(m_mobileServer, &KityMobileServer::voiceCommandReceived, this, [this](const QString &text, bool fromVoice) {
        sendMessage(text, fromVoice);
    });
    connect(this, &KityApplet::stateChanged, this, [this]() {
        if (m_mobileServer) {
            m_mobileServer->broadcastState(state(), m_statusMessage);
        }
    });
    connect(this, &KityApplet::audioLevelChanged, this, [this]() {
        if (m_mobileServer) {
            m_mobileServer->broadcastAudioLevel(m_audioLevel);
        }
    });
    m_mobileServer->start();

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
    const QString oldStatus = m_statusMessage;

    m_state = newState;
    if (!newStatus.isEmpty()) {
        m_statusMessage = newStatus;
    } else if (newState == QLatin1String("idle")) {
        m_statusMessage.clear();
    } else if (newState == QLatin1String("listening")) {
        m_statusMessage = i18n("Escuchando…");
    } else if (newState == QLatin1String("transcribing")) {
        m_statusMessage = i18n("Transcribiendo voz…");
    } else if (newState == QLatin1String("thinking")) {
        m_statusMessage = i18n("Generando respuesta…");
    } else if (newState == QLatin1String("speaking")) {
        m_statusMessage = i18n("Reproduciendo respuesta…");
    } else {
        m_statusMessage.clear();
    }

    const bool statusChangedFlag = (oldStatus != m_statusMessage);

    if (stateChangedFlag) {
        Q_EMIT stateChanged();
    }
    if (statusChangedFlag) {
        Q_EMIT statusMessageChanged();
    }

    if (newState == QLatin1String("idle")) {
        if (!m_statusMessage.isEmpty()) {
            QTimer::singleShot(3500, this, [this]() {
                if (m_state == QLatin1String("idle") && !m_statusMessage.isEmpty()) {
                    m_statusMessage.clear();
                    Q_EMIT statusMessageChanged();
                }
            });
        }
        if (m_wakeWordEnabled) {
            QTimer::singleShot(450, this, [this]() {
                if (m_state == QLatin1String("idle") && m_wakeWordEnabled) {
                    startWakeWordListener();
                }
            });
        }
    } else if (newState == QLatin1String("error")) {
        QTimer::singleShot(3500, this, [this]() {
            if (m_state == QLatin1String("error")) {
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

QString KityApplet::resolveGeminiApiKey() const
{
    QString key = readConfigString(QStringLiteral("geminiApiKey")).trimmed();
    if (key.isEmpty()) {
        key = readConfigString(QStringLiteral("googleApiKey")).trimmed();
    }
    if (key.isEmpty()) {
        key = qEnvironmentVariable("GEMINI_API_KEY").trimmed();
    }
    return key;
}

QString KityApplet::resolveGeminiModel() const
{
    return readConfigString(QStringLiteral("geminiModel"), QStringLiteral("gemini-2.0-flash")).trimmed();
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

QString KityApplet::panelIconStyle() const
{
    return readConfigString(QStringLiteral("panelIconStyle"), QStringLiteral("symbolic"));
}

void KityApplet::setPanelIconStyle(const QString &style)
{
    auto kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    kityrc->group(QStringLiteral("General")).writeEntry(QStringLiteral("panelIconStyle"), style);
    kityrc->sync();
    KConfigGroup cfg = config();
    cfg.group(QStringLiteral("General")).writeEntry(QStringLiteral("panelIconStyle"), style);
    cfg.sync();
    Q_EMIT panelIconStyleChanged();
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

    // 2. TTS engine check (Google Cloud, Edge Neural, Kokoro-82M, Piper)
    const bool hasPiperCli = !QStandardPaths::findExecutable(QStringLiteral("piper")).isEmpty();
    const bool hasPiperVoice = !resolvePiperVoicePath().isEmpty();
    const bool hasKokoroCli = !QStandardPaths::findExecutable(QStringLiteral("kity-kokoro-tts")).isEmpty();
    const bool hasEdgeCli = !QStandardPaths::findExecutable(QStringLiteral("kity-edge-tts")).isEmpty();
    const bool hasPwPlay = !QStandardPaths::findExecutable(QStringLiteral("pw-play")).isEmpty();
    const bool newPiperAvailable = ((hasPiperCli && hasPiperVoice) || hasKokoroCli || hasEdgeCli) && hasPwPlay;

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

    // Attach generative UI card if pending
    if (!m_pendingCardType.isEmpty() && !m_pendingCardData.isEmpty()) {
        assistantMsg[QStringLiteral("cardType")] = m_pendingCardType;
        assistantMsg[QStringLiteral("cardData")] = m_pendingCardData;
        m_pendingCardType.clear();
        m_pendingCardData.clear();
    }

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

    // Weather context pre-injection if query asks about weather
    const QString weatherCtx = fetchWeatherContext(trimmed);
    if (!weatherCtx.isEmpty()) {
        context.append(weatherCtx + QStringLiteral("\n\n"));
    }

    // Proactive Web Search grounding (like Gemini and Apple Intelligence):
    // Pre-inject fresh web knowledge if query asks about dates/years (2024-2030), elections, upcoming events,
    // current affairs, news, who/when/where/which, etc.
    static const QRegularExpression webSignalRx(
        QStringLiteral("(202[4-9]|203\\d|noticias?|elecci[oó]n|elecciones|gobierno|presidente|gan[oó]|ganador|partido|campe[oó]n|cu[aá]ndo|qui[eé]n|d[oó]nde|cu[aá]l|precio|estreno|actualidad|pr[oó]xim[ao]s?|hoy|ayer|mañana|suceso|falleci[oó]|resultado)"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(webSignalRx)) {
        const QString webCtx = fetchWebSearchContext(trimmed);
        if (!webCtx.isEmpty()) {
            context.append(webCtx + QStringLiteral("\n\n"));
        }
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
        QStringLiteral("(?:(?:mi|la)\\s+pantalla|en\\s+(?:mi\\s+|la\\s+)?pantalla|captura(?:r)?\\s+(?:de\\s+pantalla|la\\s+pantalla)|analiza(?:r)?\\s+(?:mi\\s+|la\\s+)?pantalla|qu[eé]\\s+hay\\s+en\\s+(?:pantalla|el\\s+escritorio)|qu[eé]\\s+ves\\s+en\\s+pantalla|qu[eé]\\s+aplicaci[oó]n\\s+se\\s+muestra)"),
        QRegularExpression::CaseInsensitiveOption);
    if (!trimmed.contains(QLatin1String("\"\"\"")) && trimmed.contains(screenRegex)) {
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

    const QString aiBackend = readConfigString(QStringLiteral("aiBackend"), QStringLiteral("hybrid")).toLower();
    bool useGemini = false;
    const QString geminiApiKey = resolveGeminiApiKey();

    if (aiBackend == QLatin1String("gemini")) {
        useGemini = true;
    } else if (aiBackend == QLatin1String("hybrid")) {
        // Hybrid intelligence:
        // Use local Ollama if prompt is a local KDE system command (volume, brightness, power, app launching, timer)
        // AND Ollama is reachable; otherwise use Google Gemini for internet, world knowledge, and deep reasoning
        static const QRegularExpression localActionSignal(
            QStringLiteral("^(?:sube|baja|ajusta|pon|silencia|abre|abrir|ejecuta|cierra|temporizador|alarma|bater[ií]a|brillo|volumen)"),
            QRegularExpression::CaseInsensitiveOption);
        const bool isLocalAction = trimmed.contains(localActionSignal);
        if (isLocalAction && m_ollamaReachable) {
            useGemini = false;
        } else if (!geminiApiKey.isEmpty()) {
            useGemini = true;
        } else {
            useGemini = false;
        }
    } else {
        useGemini = false;
    }

    if (useGemini) {
        sendToGemini(effectivePrompt, context, base64Image);
    } else {
        sendToOllama(effectivePrompt, context, base64Image);
    }
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
    const QString currentDateStr = QLocale(QLocale::Spanish, QLocale::Spain).toString(QDateTime::currentDateTime(), QStringLiteral("dddd, d 'de' MMMM 'de' yyyy, HH:mm"));
    fullSysPrompt.append(QStringLiteral("\n[Fecha y hora actual del sistema: %1]").arg(currentDateStr));
    fullSysPrompt.append(QStringLiteral(
        "\n[Acceso a Internet en tiempo real:]\n"
        "Dispones de acceso directo a Internet mediante búsqueda web ('web_search') y mediante el contexto web provisto en tiempo real. "
        "Cuando el usuario pregunte por fechas, noticias, acontecimientos actuales o futuros (año 2026 en adelante), "
        "resultados electorales o deportivos, personas o cualquier dato de actualidad, "
        "basa tu respuesta en la información de internet provista o invoca la herramienta 'web_search'. "
        "NUNCA digas que no tienes información actualizada sin antes consultar la búsqueda web."));
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

    m_currentChatMessages = chatMsgs;
    m_pendingToolCalls = QJsonArray();
    m_isToolFollowUpRound = false;

    QJsonObject reqPayload;
    reqPayload[QStringLiteral("model")] = modelToUse;
    reqPayload[QStringLiteral("messages")] = chatMsgs;
    reqPayload[QStringLiteral("stream")] = true;
    reqPayload[QStringLiteral("options")] = opts;
    reqPayload[QStringLiteral("keep_alive")] = readConfigString(QStringLiteral("ollamaKeepAlive"), QStringLiteral("24h"));

    if (!hasVision) {
        const QJsonArray tools = buildAvailableTools(userPrompt);
        if (!tools.isEmpty()) {
            reqPayload[QStringLiteral("tools")] = tools;
        }
    }

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
        if (obj.contains(QStringLiteral("error"))) {
            const QString err = obj.value(QStringLiteral("error")).toString();
            setState(QStringLiteral("error"), i18n("Error de Ollama: %1", err));
            return;
        }

        const QJsonObject msgObj = obj.value(QStringLiteral("message")).toObject();

        if (msgObj.contains(QStringLiteral("tool_calls"))) {
            const QJsonArray tCalls = msgObj.value(QStringLiteral("tool_calls")).toArray();
            for (const QJsonValue &tcVal : tCalls) {
                const QJsonObject tc = tcVal.toObject();
                const QString tcId = tc.value(QStringLiteral("id")).toString();
                bool exists = false;
                for (const QJsonValue &existing : std::as_const(m_pendingToolCalls)) {
                    if (!tcId.isEmpty() && existing.toObject().value(QStringLiteral("id")).toString() == tcId) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    m_pendingToolCalls.append(tc);
                }
            }
        }

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

    const QString httpError = m_currentOllamaReply->errorString();
    m_currentOllamaReply->deleteLater();
    m_currentOllamaReply = nullptr;

    if (error) {
        setState(QStringLiteral("error"), i18n("Error al comunicarse con Ollama: %1", httpError));
        return;
    }

    // Check if tools were invoked by the model
    if (!m_pendingToolCalls.isEmpty()) {
        // Assistant tool_calls message
        QJsonObject asstMsg;
        asstMsg[QStringLiteral("role")] = QStringLiteral("assistant");
        asstMsg[QStringLiteral("content")] = QStringLiteral("");
        asstMsg[QStringLiteral("tool_calls")] = m_pendingToolCalls;
        m_currentChatMessages.append(asstMsg);

        // Execute each tool and append tool result message
        for (const QJsonValue &tcVal : m_pendingToolCalls) {
            const QJsonObject tcObj = tcVal.toObject();
            const QString callId = tcObj.value(QStringLiteral("id")).toString();
            const QJsonObject funcObj = tcObj.value(QStringLiteral("function")).toObject();
            const QString toolName = funcObj.value(QStringLiteral("name")).toString();
            const QJsonObject toolArgs = funcObj.value(QStringLiteral("arguments")).toObject();

            if (toolName == QLatin1String("get_weather")) {
                setState(QStringLiteral("thinking"), i18n("Consultando el tiempo meteorológico…"));
            } else if (toolName == QLatin1String("media_control")) {
                setState(QStringLiteral("thinking"), i18n("Ajustando la reproducción multimedia…"));
            } else if (toolName == QLatin1String("set_timer")) {
                setState(QStringLiteral("thinking"), i18n("Configurando recordatorio o temporizador…"));
            } else if (toolName == QLatin1String("get_system_vitals")) {
                setState(QStringLiteral("thinking"), i18n("Comprobando el rendimiento y recursos del sistema…"));
            } else if (toolName == QLatin1String("launch_application")) {
                const QString app = toolArgs.value(QStringLiteral("app_name")).toString();
                setState(QStringLiteral("thinking"), app.isEmpty() ? i18n("Abriendo aplicación…") : i18n("Abriendo %1…", app));
            } else if (toolName == QLatin1String("web_search")) {
                setState(QStringLiteral("thinking"), i18n("Buscando información actualizada en la web…"));
            } else if (toolName == QLatin1String("inspect_screen")) {
                setState(QStringLiteral("thinking"), i18n("Inspeccionando el contenido de la pantalla…"));
            } else {
                setState(QStringLiteral("thinking"), i18n("Ejecutando acción del sistema…"));
            }

            const QJsonObject result = executeToolCall(toolName, toolArgs);

            QJsonObject toolMsg;
            toolMsg[QStringLiteral("role")] = QStringLiteral("tool");
            if (!callId.isEmpty()) {
                toolMsg[QStringLiteral("tool_call_id")] = callId;
            }
            toolMsg[QStringLiteral("content")] = QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
            m_currentChatMessages.append(toolMsg);
        }

        m_pendingToolCalls = QJsonArray();
        m_streamingText.clear();

        sendOllamaToolFollowUp(m_currentChatMessages);
        return;
    }

    QString finalResponse = m_streamingText.trimmed();
    // Strip reasoning tags (<think>...</think>) if present
    static const QRegularExpression thinkRegex(QStringLiteral("<think>.*?</think>"), QRegularExpression::DotMatchesEverythingOption);
    finalResponse.remove(thinkRegex);
    finalResponse = finalResponse.trimmed();

    // If the model wrapped its response in a pseudo JSON tool call (e.g. {"name":"...","parameters":{"text":"..."}})
    if (finalResponse.startsWith(QLatin1Char('{')) && finalResponse.contains(QStringLiteral("\"parameters\""))) {
        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(finalResponse.toUtf8(), &parseErr);
        if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject obj = doc.object();
            if (obj.contains(QStringLiteral("parameters")) && obj.value(QStringLiteral("parameters")).isObject()) {
                const QJsonObject params = obj.value(QStringLiteral("parameters")).toObject();
                if (params.contains(QStringLiteral("text"))) {
                    finalResponse = params.value(QStringLiteral("text")).toString().trimmed();
                }
            }
        }
    }

    if (!finalResponse.isEmpty()) {
        QVariantMap assistantMsg;
        assistantMsg[QStringLiteral("role")] = QStringLiteral("assistant");
        assistantMsg[QStringLiteral("content")] = finalResponse;
        assistantMsg[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);

        // Extract any Markdown links as structured sources
        static const QRegularExpression mdLinkRx(QStringLiteral("\\[([^\\]]+)\\]\\((https?://[^\\)]+)\\)"));
        auto linkIt = mdLinkRx.globalMatch(finalResponse);
        QVariantList srcList;
        QStringList seenUrls;
        while (linkIt.hasNext()) {
            auto m = linkIt.next();
            const QString title = m.captured(1).trimmed();
            const QString url = m.captured(2).trimmed();
            if (!seenUrls.contains(url)) {
                seenUrls.append(url);
                QVariantMap s;
                s[QStringLiteral("title")] = title;
                s[QStringLiteral("url")] = url;
                srcList.append(s);
            }
        }
        if (!srcList.isEmpty()) {
            assistantMsg[QStringLiteral("sources")] = srcList;
        }

        // Attach generative UI card if pending
        if (!m_pendingCardType.isEmpty() && !m_pendingCardData.isEmpty()) {
            assistantMsg[QStringLiteral("cardType")] = m_pendingCardType;
            assistantMsg[QStringLiteral("cardData")] = m_pendingCardData;
            m_pendingCardType.clear();
            m_pendingCardData.clear();
        }

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

void KityApplet::sendOllamaToolFollowUp(const QJsonArray &messages)
{
    if (!m_ollamaReachable) {
        setState(QStringLiteral("error"), i18n("No se puede conectar con el servidor Ollama en %1.", resolveOllamaUrl()));
        return;
    }

    setState(QStringLiteral("thinking"), i18n("Generando respuesta…"));
    m_streamingText.clear();
    m_ttsSpeechBuffer.clear();
    Q_EMIT streamingTextChanged();

    const QString modelToUse = resolveOllamaModel();

    QJsonObject opts;
    opts[QStringLiteral("temperature")] = readConfigDouble(QStringLiteral("ollamaTemperature"), 0.2);
    opts[QStringLiteral("num_ctx")] = readConfigInt(QStringLiteral("ollamaNumCtx"), 4096);
    opts[QStringLiteral("num_thread")] = readConfigInt(QStringLiteral("ollamaNumThread"), 6);
    opts[QStringLiteral("num_predict")] = readConfigInt(QStringLiteral("ollamaNumPredict"), 384);

    QJsonObject reqPayload;
    reqPayload[QStringLiteral("model")] = modelToUse;
    reqPayload[QStringLiteral("messages")] = messages;
    reqPayload[QStringLiteral("stream")] = true;
    reqPayload[QStringLiteral("options")] = opts;
    reqPayload[QStringLiteral("keep_alive")] = readConfigString(QStringLiteral("ollamaKeepAlive"), QStringLiteral("24h"));

    const QJsonArray tools = buildAvailableTools(QString());
    if (!tools.isEmpty()) {
        reqPayload[QStringLiteral("tools")] = tools;
    }

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

void KityApplet::sendToGemini(const QString &userPrompt, const QString &injectedContext, const QString &base64Image)
{
    const QString apiKey = resolveGeminiApiKey();
    if (apiKey.isEmpty()) {
        setState(QStringLiteral("error"), i18n("No se ha configurado la clave API de Google Gemini. Añádela en las Preferencias de Kity."));
        return;
    }

    setState(QStringLiteral("thinking"), i18n("Consultando con Google Gemini…"));
    m_streamingText.clear();
    m_ttsSpeechBuffer.clear();
    m_geminiSources.clear();
    Q_EMIT streamingTextChanged();

    const QString modelToUse = resolveGeminiModel();
    const bool hasVision = !base64Image.isEmpty();

    // 1. System instruction
    QString fullSysPrompt = resolveSystemPrompt();
    if (!fullSysPrompt.contains(QStringLiteral("español"), Qt::CaseInsensitive)) {
        fullSysPrompt.prepend(QStringLiteral("REGLA ESTRICTA DE IDIOMA: Responde SIEMPRE y exclusivamente en español de España (castellano).\n\n"));
    }
    const QString currentDateStr = QLocale(QLocale::Spanish, QLocale::Spain).toString(QDateTime::currentDateTime(), QStringLiteral("dddd, d 'de' MMMM 'de' yyyy, HH:mm"));
    fullSysPrompt.append(QStringLiteral("\n[Fecha y hora actual del sistema: %1]").arg(currentDateStr));
    const QString vitals = collectSystemVitals();
    if (!vitals.isEmpty()) {
        fullSysPrompt.append(QStringLiteral("\n[Entorno del sistema: %1]").arg(vitals));
    }
    if (!injectedContext.isEmpty()) {
        fullSysPrompt.append(QStringLiteral("\n[Contexto relevante:\n%1]").arg(injectedContext.trimmed()));
    }

    QJsonObject sysInstruction;
    QJsonArray sysParts;
    QJsonObject sysPart;
    sysPart[QStringLiteral("text")] = fullSysPrompt;
    sysParts.append(sysPart);
    sysInstruction[QStringLiteral("parts")] = sysParts;

    // 2. Chat contents
    QJsonArray contents;
    const int startIdx = qMax(0, m_messages.size() - 8);
    for (int i = startIdx; i < m_messages.size() - 1; ++i) {
        const QVariantMap vm = m_messages.at(i).toMap();
        const QString role = vm.value(QStringLiteral("role")).toString();
        const QString content = vm.value(QStringLiteral("content")).toString();
        if (content.trimmed().isEmpty()) continue;

        QJsonObject msgObj;
        msgObj[QStringLiteral("role")] = (role == QLatin1String("assistant")) ? QStringLiteral("model") : QStringLiteral("user");
        QJsonArray parts;
        QJsonObject textPart;
        textPart[QStringLiteral("text")] = content;
        parts.append(textPart);
        msgObj[QStringLiteral("parts")] = parts;
        contents.append(msgObj);
    }

    // Current turn
    QJsonObject currentTurn;
    currentTurn[QStringLiteral("role")] = QStringLiteral("user");
    QJsonArray currentParts;
    QJsonObject userTextPart;
    userTextPart[QStringLiteral("text")] = userPrompt;
    currentParts.append(userTextPart);

    if (hasVision) {
        QJsonObject inlineData;
        inlineData[QStringLiteral("mime_type")] = QStringLiteral("image/png");
        inlineData[QStringLiteral("data")] = base64Image;
        QJsonObject imgPart;
        imgPart[QStringLiteral("inline_data")] = inlineData;
        currentParts.append(imgPart);
    }
    currentTurn[QStringLiteral("parts")] = currentParts;
    contents.append(currentTurn);

    // 3. Payload
    QJsonObject payload;
    payload[QStringLiteral("system_instruction")] = sysInstruction;
    payload[QStringLiteral("contents")] = contents;

    QJsonObject genConfig;
    genConfig[QStringLiteral("temperature")] = readConfigDouble(QStringLiteral("ollamaTemperature"), 0.2);
    genConfig[QStringLiteral("maxOutputTokens")] = 2048;
    payload[QStringLiteral("generationConfig")] = genConfig;

    // Native Google Search Grounding tool
    QJsonArray tools;
    QJsonObject searchTool;
    searchTool[QStringLiteral("google_search")] = QJsonObject();
    tools.append(searchTool);
    payload[QStringLiteral("tools")] = tools;

    const QUrl geminiUrl(QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models/%1:streamGenerateContent?key=%2&alt=sse")
        .arg(modelToUse, apiKey));

    QNetworkRequest req(geminiUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    if (m_currentGeminiReply) {
        m_currentGeminiReply->abort();
        m_currentGeminiReply->deleteLater();
        m_currentGeminiReply = nullptr;
    }

    m_currentGeminiReply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(m_currentGeminiReply, &QNetworkReply::readyRead, this, &KityApplet::handleGeminiStreamChunk);
    connect(m_currentGeminiReply, &QNetworkReply::finished, this, &KityApplet::handleGeminiFinished);
}

void KityApplet::handleGeminiStreamChunk()
{
    if (!m_currentGeminiReply) return;

    while (m_currentGeminiReply->canReadLine()) {
        QByteArray line = m_currentGeminiReply->readLine().trimmed();
        if (line.isEmpty()) continue;

        if (line.startsWith("data: ")) {
            line = line.mid(6).trimmed();
        }

        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) continue;

        const QJsonObject obj = doc.object();
        if (obj.contains(QStringLiteral("error"))) {
            const QJsonObject errObj = obj.value(QStringLiteral("error")).toObject();
            const QString errMsg = errObj.value(QStringLiteral("message")).toString();
            setState(QStringLiteral("error"), i18n("Error de Google Gemini: %1", errMsg));
            return;
        }

        const QJsonArray candidates = obj.value(QStringLiteral("candidates")).toArray();
        if (candidates.isEmpty()) continue;

        const QJsonObject candidate = candidates.first().toObject();

        // Extract Google Search Grounding metadata and source citations
        if (candidate.contains(QStringLiteral("groundingMetadata"))) {
            const QJsonObject gMeta = candidate.value(QStringLiteral("groundingMetadata")).toObject();
            if (gMeta.contains(QStringLiteral("groundingChunks"))) {
                const QJsonArray chunks = gMeta.value(QStringLiteral("groundingChunks")).toArray();
                for (const QJsonValue &chunkVal : chunks) {
                    const QJsonObject webObj = chunkVal.toObject().value(QStringLiteral("web")).toObject();
                    const QString uri = webObj.value(QStringLiteral("uri")).toString();
                    QString title = webObj.value(QStringLiteral("title")).toString();
                    if (title.isEmpty()) {
                        title = QUrl(uri).host();
                    }
                    if (!uri.isEmpty()) {
                        bool exists = false;
                        for (const auto &pair : m_geminiSources) {
                            if (pair.second == uri) {
                                exists = true;
                                break;
                            }
                        }
                        if (!exists) {
                            m_geminiSources.append(qMakePair(title, uri));
                        }
                    }
                }
            }
        }

        const QJsonObject content = candidate.value(QStringLiteral("content")).toObject();
        const QJsonArray parts = content.value(QStringLiteral("parts")).toArray();
        for (const QJsonValue &partVal : parts) {
            const QJsonObject partObj = partVal.toObject();
            const QString textChunk = partObj.value(QStringLiteral("text")).toString();
            if (!textChunk.isEmpty()) {
                m_streamingText.append(textChunk);
                Q_EMIT streamingTextChanged();
                processStreamingTtsChunk(textChunk);
            }
        }
    }
}

void KityApplet::handleGeminiFinished()
{
    if (!m_currentGeminiReply) return;

    const bool error = (m_currentGeminiReply->error() != QNetworkReply::NoError &&
                        m_currentGeminiReply->error() != QNetworkReply::OperationCanceledError);

    const QString httpError = m_currentGeminiReply->errorString();
    m_currentGeminiReply->deleteLater();
    m_currentGeminiReply = nullptr;

    if (error) {
        setState(QStringLiteral("error"), i18n("Error al comunicarse con Google Gemini: %1", httpError));
        return;
    }

    QString finalResponse = m_streamingText.trimmed();
    static const QRegularExpression thinkRegex(QStringLiteral("<think>.*?</think>"), QRegularExpression::DotMatchesEverythingOption);
    finalResponse.remove(thinkRegex);
    finalResponse = finalResponse.trimmed();

    if (!m_geminiSources.isEmpty()) {
        static const QRegularExpression fuentesSignal(
            QStringLiteral("(?:fuentes|referencias|enlaces|sources):"),
            QRegularExpression::CaseInsensitiveOption);
        if (!finalResponse.contains(fuentesSignal)) {
            QString sourcesBlock = QStringLiteral("\n\n🌐 **Fuentes consultadas:**\n");
            for (const auto &pair : m_geminiSources) {
                sourcesBlock.append(QStringLiteral("- [%1](%2)\n").arg(pair.first, pair.second));
            }
            finalResponse.append(sourcesBlock.trimmed());
        }
    }

    if (!finalResponse.isEmpty()) {
        QVariantMap assistantMsg;
        assistantMsg[QStringLiteral("role")] = QStringLiteral("assistant");
        assistantMsg[QStringLiteral("content")] = finalResponse;
        assistantMsg[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);

        QVariantList srcList;
        for (const auto &pair : m_geminiSources) {
            QVariantMap s;
            s[QStringLiteral("title")] = pair.first;
            s[QStringLiteral("url")] = pair.second;
            srcList.append(s);
        }
        if (!srcList.isEmpty()) {
            assistantMsg[QStringLiteral("sources")] = srcList;
        }

        // Attach generative UI card if pending
        if (!m_pendingCardType.isEmpty() && !m_pendingCardData.isEmpty()) {
            assistantMsg[QStringLiteral("cardType")] = m_pendingCardType;
            assistantMsg[QStringLiteral("cardData")] = m_pendingCardData;
            m_pendingCardType.clear();
            m_pendingCardData.clear();
        }

        m_messages.append(assistantMsg);
        Q_EMIT messagesChanged();
        saveConversationHistory();

        if (!m_ttsSpeechBuffer.isEmpty()) {
            enqueueSentenceForSpeech(m_ttsSpeechBuffer.trimmed());
            m_ttsSpeechBuffer.clear();
        }
    }

    m_streamingText.clear();
    Q_EMIT streamingTextChanged();

    if (m_isSpeakingSentence || !m_ttsQueue.isEmpty()) {
        setState(QStringLiteral("speaking"), i18n("Reproduciendo respuesta…"));
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
    if (m_currentGeminiReply) {
        m_currentGeminiReply->abort();
        m_currentGeminiReply->deleteLater();
        m_currentGeminiReply = nullptr;
    }
    m_pendingToolCalls = QJsonArray();
    m_currentChatMessages = QJsonArray();
    m_isToolFollowUpRound = false;
    stopSpeaking();
    setState(QStringLiteral("idle"), i18n("Generación cancelada."));
}

QJsonArray KityApplet::buildAvailableTools(const QString &userPrompt) const
{
    QJsonArray tools;

    // Do not inject function tools for writing tools or text transformation prompts
    if (userPrompt.contains(QStringLiteral("Texto original:\n\"\"\"")) || userPrompt.startsWith(QStringLiteral("Eres un asistente"))) {
        return tools;
    }

    auto createTool = [](const QString &name, const QString &description, const QJsonObject &parameters) {
        QJsonObject fn;
        fn[QStringLiteral("name")] = name;
        fn[QStringLiteral("description")] = description;
        fn[QStringLiteral("parameters")] = parameters;

        QJsonObject tool;
        tool[QStringLiteral("type")] = QStringLiteral("function");
        tool[QStringLiteral("function")] = fn;
        return tool;
    };

    const QString model = resolveOllamaModel().toLower();
    const bool isLlama32 = model.contains(QStringLiteral("llama3.2"));
    const QString q = userPrompt.toLower().trimmed();

    // Tool 1: get_weather
    static const QRegularExpression weatherSignal(
        QStringLiteral("(tiempo|clima|temperatura|llueve|llover|lluvia|fr[ií]o|calor|grados|pron[oó]stico|viento|soleado|nublado)"),
        QRegularExpression::CaseInsensitiveOption);
    if (!isLlama32 || q.isEmpty() || q.contains(weatherSignal)) {
        QJsonObject props;
        QJsonObject cityProp;
        cityProp[QStringLiteral("type")] = QStringLiteral("string");
        cityProp[QStringLiteral("description")] = QStringLiteral("Nombre de la ciudad o localidad (ej. Gijón, Oviedo, Madrid, París). Si el usuario no menciona ciudad, omitir.");
        props[QStringLiteral("city")] = cityProp;

        QJsonObject params;
        params[QStringLiteral("type")] = QStringLiteral("object");
        params[QStringLiteral("properties")] = props;

        tools.append(createTool(
            QStringLiteral("get_weather"),
            QStringLiteral("Obtiene las condiciones meteorológicas y previsión del tiempo actual para una ciudad o localidad."),
            params
        ));
    }

    // Tool 2: media_control
    static const QRegularExpression mediaSignal(
        QStringLiteral("(m[uú]sica|canci[oó]n|reproduce|reproducir|pausa|pausar|player|spotify|cancion|tema|pista|volumen|sonido)"),
        QRegularExpression::CaseInsensitiveOption);
    if (!isLlama32 || q.isEmpty() || q.contains(mediaSignal)) {
        QJsonObject props;
        QJsonObject actProp;
        actProp[QStringLiteral("type")] = QStringLiteral("string");
        QJsonArray actEnum;
        actEnum.append(QStringLiteral("play"));
        actEnum.append(QStringLiteral("pause"));
        actEnum.append(QStringLiteral("play_pause"));
        actEnum.append(QStringLiteral("next"));
        actEnum.append(QStringLiteral("previous"));
        actEnum.append(QStringLiteral("status"));
        actProp[QStringLiteral("enum")] = actEnum;
        actProp[QStringLiteral("description")] = QStringLiteral("Acción multimedia a ejecutar.");
        props[QStringLiteral("action")] = actProp;

        QJsonObject params;
        params[QStringLiteral("type")] = QStringLiteral("object");
        params[QStringLiteral("properties")] = props;
        QJsonArray req;
        req.append(QStringLiteral("action"));
        params[QStringLiteral("required")] = req;

        tools.append(createTool(
            QStringLiteral("media_control"),
            QStringLiteral("Controla la reproducción multimedia activa en el sistema (reproducir, pausar, siguiente pista, pista anterior o consultar qué suena)."),
            params
        ));
    }

    // Tool 3: set_timer
    static const QRegularExpression timerSignal(
        QStringLiteral("(temporizador|alarma|av[ií]same|cuenta\\s+atr[aá]s|recu[eé]rdame|minuto|segundo|recordatorio)"),
        QRegularExpression::CaseInsensitiveOption);
    if (!isLlama32 || q.isEmpty() || q.contains(timerSignal)) {
        QJsonObject props;
        QJsonObject secProp;
        secProp[QStringLiteral("type")] = QStringLiteral("integer");
        secProp[QStringLiteral("description")] = QStringLiteral("Duración del temporizador en segundos totales (por ejemplo 300 para 5 minutos).");
        props[QStringLiteral("seconds")] = secProp;

        QJsonObject lblProp;
        lblProp[QStringLiteral("type")] = QStringLiteral("string");
        lblProp[QStringLiteral("description")] = QStringLiteral("Motivo o etiqueta del temporizador (ej. 'pizza', 'pasta', 'reunión').");
        props[QStringLiteral("label")] = lblProp;

        QJsonObject params;
        params[QStringLiteral("type")] = QStringLiteral("object");
        params[QStringLiteral("properties")] = props;
        QJsonArray req;
        req.append(QStringLiteral("seconds"));
        params[QStringLiteral("required")] = req;

        tools.append(createTool(
            QStringLiteral("set_timer"),
            QStringLiteral("Programa un temporizador o alarma en el sistema para que avise al usuario tras un número de segundos."),
            params
        ));
    }

    // Tool 4: get_system_vitals
    static const QRegularExpression sysSignal(
        QStringLiteral("(sistema|bater[ií]a|memoria|ram|cpu|temperatura\\s+del\\s+pc|rendimiento|recursos|hardware)"),
        QRegularExpression::CaseInsensitiveOption);
    if (!isLlama32 || q.isEmpty() || q.contains(sysSignal)) {
        QJsonObject params;
        params[QStringLiteral("type")] = QStringLiteral("object");
        params[QStringLiteral("properties")] = QJsonObject();

        tools.append(createTool(
            QStringLiteral("get_system_vitals"),
            QStringLiteral("Consulta el estado en tiempo real de los recursos del equipo: memoria RAM libre y total, uso de CPU, batería y perfil de energía."),
            params
        ));
    }

    // Tool 5: launch_application
    static const QRegularExpression appSignal(
        QStringLiteral("(abre|abrir|ejecuta|ejecutar|inicia|iniciar|lanza|lanzar|arranca|arrancar)\\s+"),
        QRegularExpression::CaseInsensitiveOption);
    if (!isLlama32 || q.isEmpty() || q.contains(appSignal)) {
        QJsonObject props;
        QJsonObject appProp;
        appProp[QStringLiteral("type")] = QStringLiteral("string");
        appProp[QStringLiteral("description")] = QStringLiteral("Nombre común o ejecutable de la aplicación a abrir (ej. 'firefox', 'spotify', 'dolphin', 'konsole', 'kate').");
        props[QStringLiteral("app_name")] = appProp;

        QJsonObject params;
        params[QStringLiteral("type")] = QStringLiteral("object");
        params[QStringLiteral("properties")] = props;
        QJsonArray req;
        req.append(QStringLiteral("app_name"));
        params[QStringLiteral("required")] = req;

        tools.append(createTool(
            QStringLiteral("launch_application"),
            QStringLiteral("Abre o ejecuta una aplicación de escritorio instalada en el sistema KDE Plasma."),
            params
        ));
    }

    // Tool 6: web_search (Always available, just like Siri and Gemini)
    {
        QJsonObject props;
        QJsonObject qProp;
        qProp[QStringLiteral("type")] = QStringLiteral("string");
        qProp[QStringLiteral("description")] = QStringLiteral("Términos o palabras clave de la búsqueda en la web.");
        props[QStringLiteral("query")] = qProp;

        QJsonObject params;
        params[QStringLiteral("type")] = QStringLiteral("object");
        params[QStringLiteral("properties")] = props;
        QJsonArray req;
        req.append(QStringLiteral("query"));
        params[QStringLiteral("required")] = req;

        tools.append(createTool(
            QStringLiteral("web_search"),
            QStringLiteral("Busca en Internet en tiempo real noticias, fechas, elecciones, eventos, datos de 2024 a 2026 en adelante, personas, resultados deportivos o cualquier información actualizada cuando no esté en la memoria local."),
            params
        ));
    }

    // Tool 7: inspect_screen
    static const QRegularExpression screenSignal(
        QStringLiteral("(pantalla|screen|ventana|captura|escritorio|lee|texto|c[oó]digo|error|abiert[ao]|viendo)"),
        QRegularExpression::CaseInsensitiveOption);
    if (!isLlama32 || q.isEmpty() || q.contains(screenSignal)) {
        QJsonObject props;
        QJsonObject modeProp;
        modeProp[QStringLiteral("type")] = QStringLiteral("string");
        QJsonArray modeEnum;
        modeEnum.append(QStringLiteral("active_window"));
        modeEnum.append(QStringLiteral("fullscreen"));
        modeProp[QStringLiteral("enum")] = modeEnum;
        modeProp[QStringLiteral("description")] = QStringLiteral("Modo de inspección: 'active_window' para la ventana enfocada o activa, o 'fullscreen' para todo el escritorio.");
        props[QStringLiteral("mode")] = modeProp;

        QJsonObject params;
        params[QStringLiteral("type")] = QStringLiteral("object");
        params[QStringLiteral("properties")] = props;

        tools.append(createTool(
            QStringLiteral("inspect_screen"),
            QStringLiteral("Captura e inspecciona la pantalla o la ventana activa mediante OCR y metadatos del compositor KWin Wayland para leer texto visible, código fuente, errores de consola o averiguar qué aplicaciones y ventanas tiene abiertas el usuario."),
            params
        ));
    }

    // Tool 8: calendar_event (KOrganizer / KDE Calendar Intent)
    static const QRegularExpression calSignal(
        QStringLiteral("(calendario|evento|reuni[oó]n|cita|agenda|agendar|korganizer|organizer|compromiso)"),
        QRegularExpression::CaseInsensitiveOption);
    if (!isLlama32 || q.isEmpty() || q.contains(calSignal)) {
        QJsonObject props;
        QJsonObject actProp;
        actProp[QStringLiteral("type")] = QStringLiteral("string");
        QJsonArray actEnum;
        actEnum.append(QStringLiteral("create"));
        actEnum.append(QStringLiteral("list"));
        actProp[QStringLiteral("enum")] = actEnum;
        actProp[QStringLiteral("description")] = QStringLiteral("Acción a realizar: 'create' para agendar un nuevo evento o 'list' para consultar eventos existentes.");
        props[QStringLiteral("action")] = actProp;

        QJsonObject titleProp;
        titleProp[QStringLiteral("type")] = QStringLiteral("string");
        titleProp[QStringLiteral("description")] = QStringLiteral("Título o asunto del evento (ej. 'Reunión de proyecto', 'Cita médica').");
        props[QStringLiteral("title")] = titleProp;

        QJsonObject timeProp;
        timeProp[QStringLiteral("type")] = QStringLiteral("string");
        timeProp[QStringLiteral("description")] = QStringLiteral("Fecha y hora de inicio (ej. '2026-11-10 11:00', 'mañana a las 16:00', 'hoy a las 20:00').");
        props[QStringLiteral("start_time")] = timeProp;

        QJsonObject durProp;
        durProp[QStringLiteral("type")] = QStringLiteral("integer");
        durProp[QStringLiteral("description")] = QStringLiteral("Duración en minutos (por defecto 60).");
        props[QStringLiteral("duration_minutes")] = durProp;

        QJsonObject descProp;
        descProp[QStringLiteral("type")] = QStringLiteral("string");
        descProp[QStringLiteral("description")] = QStringLiteral("Descripción o notas adicionales del evento.");
        props[QStringLiteral("description")] = descProp;

        QJsonObject params;
        params[QStringLiteral("type")] = QStringLiteral("object");
        params[QStringLiteral("properties")] = props;
        QJsonArray req;
        req.append(QStringLiteral("action"));
        params[QStringLiteral("required")] = req;

        tools.append(createTool(
            QStringLiteral("calendar_event"),
            QStringLiteral("Gestiona la agenda y el calendario de KDE (KOrganizer): permite crear eventos, citas y reuniones o consultar la lista de eventos programados."),
            params
        ));
    }

    // Tool 9: clipboard_tool (Klipper KDE Clipboard Manager Intent)
    static const QRegularExpression clipSignal(
        QStringLiteral("(portapapeles|clipboard|copiado|copi[eé]|klipper|historial)"),
        QRegularExpression::CaseInsensitiveOption);
    if (!isLlama32 || q.isEmpty() || q.contains(clipSignal)) {
        QJsonObject props;
        QJsonObject actProp;
        actProp[QStringLiteral("type")] = QStringLiteral("string");
        QJsonArray actEnum;
        actEnum.append(QStringLiteral("get_current"));
        actEnum.append(QStringLiteral("list_history"));
        actEnum.append(QStringLiteral("search"));
        actProp[QStringLiteral("enum")] = actEnum;
        actProp[QStringLiteral("description")] = QStringLiteral("Acción: 'get_current' (obtener lo último copiado), 'list_history' (ver historial reciente) o 'search' (buscar texto copiado).");
        props[QStringLiteral("action")] = actProp;

        QJsonObject qProp;
        qProp[QStringLiteral("type")] = QStringLiteral("string");
        qProp[QStringLiteral("description")] = QStringLiteral("Texto o término a buscar en el historial del portapapeles cuando action='search'.");
        props[QStringLiteral("query")] = qProp;

        QJsonObject params;
        params[QStringLiteral("type")] = QStringLiteral("object");
        params[QStringLiteral("properties")] = props;
        QJsonArray req;
        req.append(QStringLiteral("action"));
        params[QStringLiteral("required")] = req;

        tools.append(createTool(
            QStringLiteral("clipboard_tool"),
            QStringLiteral("Accede al gestor del portapapeles de KDE Plasma (Klipper): lee el contenido actual copiado, consulta el historial reciente de copias o busca fragmentos de texto copiados con anterioridad."),
            params
        ));
    }

    // Tool 10: email_tool (KMail / Email Client Intent)
    static const QRegularExpression mailSignal(
        QStringLiteral("(correo|email|mail|escribe\\s+a|enviar\\s+correo|mandar\\s+un\\s+correo)"),
        QRegularExpression::CaseInsensitiveOption);
    if (!isLlama32 || q.isEmpty() || q.contains(mailSignal)) {
        QJsonObject props;
        QJsonObject toProp;
        toProp[QStringLiteral("type")] = QStringLiteral("string");
        toProp[QStringLiteral("description")] = QStringLiteral("Destinatario o dirección de correo (ej. 'juan@example.com' o nombre del contacto).");
        props[QStringLiteral("to")] = toProp;

        QJsonObject subjProp;
        subjProp[QStringLiteral("type")] = QStringLiteral("string");
        subjProp[QStringLiteral("description")] = QStringLiteral("Asunto del correo electrónico.");
        props[QStringLiteral("subject")] = subjProp;

        QJsonObject bodyProp;
        bodyProp[QStringLiteral("type")] = QStringLiteral("string");
        bodyProp[QStringLiteral("description")] = QStringLiteral("Cuerpo o mensaje del correo.");
        props[QStringLiteral("body")] = bodyProp;

        QJsonObject params;
        params[QStringLiteral("type")] = QStringLiteral("object");
        params[QStringLiteral("properties")] = props;

        tools.append(createTool(
            QStringLiteral("email_tool"),
            QStringLiteral("Prepara y redacta un correo electrónico en el cliente de correo del sistema (KMail, Thunderbird o interfaz web) con destinatario, asunto y mensaje."),
            params
        ));
    }

    return tools;
}

QJsonObject KityApplet::executeToolCall(const QString &toolName, const QJsonObject &arguments)
{
    QJsonObject result;

    if (toolName == QLatin1String("get_weather")) {
        QString city = arguments.value(QStringLiteral("city")).toString().trimmed();
        if (city.isEmpty()) {
            city = resolveDefaultLocation();
        }
        const QString weatherReport = fetchWeatherContext(QStringLiteral("tiempo en %1").arg(city));
        result[QStringLiteral("city")] = city;
        result[QStringLiteral("weather_report")] = weatherReport.isEmpty() ?
            i18n("No se pudieron obtener datos meteorológicos para %1 en este momento.", city) : weatherReport;
        return result;
    }

    if (toolName == QLatin1String("media_control")) {
        const QString action = arguments.value(QStringLiteral("action")).toString().trimmed().toLower();
        result[QStringLiteral("action")] = action;

        if (action == QLatin1String("play")) {
            QProcess::execute(QStringLiteral("playerctl"), QStringList() << QStringLiteral("play"));
            result[QStringLiteral("status")] = QStringLiteral("Reproducción reanudada.");
        } else if (action == QLatin1String("pause")) {
            QProcess::execute(QStringLiteral("playerctl"), QStringList() << QStringLiteral("pause"));
            result[QStringLiteral("status")] = QStringLiteral("Reproducción pausada.");
        } else if (action == QLatin1String("play_pause")) {
            QProcess::execute(QStringLiteral("playerctl"), QStringList() << QStringLiteral("play-pause"));
            result[QStringLiteral("status")] = QStringLiteral("Alternado estado de reproducción.");
        } else if (action == QLatin1String("next")) {
            QProcess::execute(QStringLiteral("playerctl"), QStringList() << QStringLiteral("next"));
            result[QStringLiteral("status")] = QStringLiteral("Siguiente pista reproducida.");
        } else if (action == QLatin1String("previous")) {
            QProcess::execute(QStringLiteral("playerctl"), QStringList() << QStringLiteral("previous"));
            result[QStringLiteral("status")] = QStringLiteral("Pista anterior reproducida.");
        } else {
            // Status check
            QProcess pStat;
            pStat.start(QStringLiteral("playerctl"), QStringList() << QStringLiteral("status"));
            pStat.waitForFinished(1000);
            const QString stat = QString::fromUtf8(pStat.readAllStandardOutput()).trimmed();

            QProcess pMeta;
            pMeta.start(QStringLiteral("playerctl"), QStringList() << QStringLiteral("metadata") << QStringLiteral("--format") << QStringLiteral("{{ artist }} - {{ title }}"));
            pMeta.waitForFinished(1000);
            const QString track = QString::fromUtf8(pMeta.readAllStandardOutput()).trimmed();

            result[QStringLiteral("status")] = stat.isEmpty() ? QStringLiteral("Sin reproductor activo") : stat;
            result[QStringLiteral("track")] = track;
        }
        return result;
    }

    if (toolName == QLatin1String("set_timer")) {
        int seconds = arguments.value(QStringLiteral("seconds")).toInt();
        if (seconds <= 0) {
            seconds = 60;
        }
        QString label = arguments.value(QStringLiteral("label")).toString().trimmed();
        if (label.isEmpty()) {
            label = i18n("Temporizador");
        }

        // Schedule one-shot timer for desktop notification
        QTimer::singleShot(seconds * 1000, this, [this, label]() {
            KNotification::event(QStringLiteral("notification"),
                                 i18n("¡Temporizador de Kity finalizado!"),
                                 label);
            speakText(i18n("El temporizador de %1 ha finalizado.", label));
        });

        // Also record in scheduled reminders list
        ScheduledReminder rem;
        rem.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        rem.text = label;
        rem.targetTime = QDateTime::currentDateTime().addSecs(seconds);
        rem.notified = false;
        m_scheduledReminders.append(rem);
        saveScheduledReminders();

        result[QStringLiteral("success")] = true;
        result[QStringLiteral("seconds")] = seconds;
        result[QStringLiteral("label")] = label;
        result[QStringLiteral("expires_at")] = rem.targetTime.toString(Qt::ISODate);
        return result;
    }

    if (toolName == QLatin1String("get_system_vitals")) {
        result[QStringLiteral("vitals")] = collectSystemVitals();
        return result;
    }

    if (toolName == QLatin1String("launch_application")) {
        const QString appQuery = arguments.value(QStringLiteral("app_name")).toString().trimmed();
        QString launchedName;
        const bool ok = launchApplication(appQuery, &launchedName);
        result[QStringLiteral("success")] = ok;
        result[QStringLiteral("requested_app")] = appQuery;
        result[QStringLiteral("launched_app")] = launchedName.isEmpty() ? appQuery : launchedName;
        return result;
    }

    if (toolName == QLatin1String("web_search")) {
        const QString query = arguments.value(QStringLiteral("query")).toString().trimmed();
        const QString searchRes = fetchWebSearchContext(query);
        result[QStringLiteral("query")] = query;
        result[QStringLiteral("results")] = searchRes.isEmpty() ? QStringLiteral("No se encontraron resultados en la web.") : searchRes;
        return result;
    }

    if (toolName == QLatin1String("inspect_screen")) {
        const QString modeArg = arguments.value(QStringLiteral("mode")).toString().trimmed();
        const QString captureMode = (modeArg == QLatin1String("fullscreen")) ? QStringLiteral("fullscreen") : QStringLiteral("activewindow");

        const QString kwinWindows = collectKWinWindowsInfo();
        const QString tmpShot = QDir::tempPath() + QStringLiteral("/kity_screen_tool_%1.png").arg(QDateTime::currentMSecsSinceEpoch());
        captureScreenToPng(tmpShot, captureMode);
        const QString ocrText = extractScreenOcrText(tmpShot);
        QFile::remove(tmpShot);

        result[QStringLiteral("capture_mode")] = captureMode;
        result[QStringLiteral("kwin_windows")] = kwinWindows.isEmpty() ? QStringLiteral("No se obtuvieron metadatos de ventanas KWin.") : kwinWindows;
        result[QStringLiteral("screen_ocr_text")] = ocrText.isEmpty() ? QStringLiteral("No se detectó texto legible por OCR en la captura.") : ocrText;
        return result;
    }

    if (toolName == QLatin1String("calendar_event")) {
        const QString action = arguments.value(QStringLiteral("action")).toString().trimmed().toLower();
        if (action == QLatin1String("list")) {
            const QDateTime now = QDateTime::currentDateTime();
            QStringList eventList;
            for (const auto &r : m_scheduledReminders) {
                if (r.targetTime >= now) {
                    const QString timeStr = r.targetTime.toString(QStringLiteral("dd/MM/yyyy HH:mm"));
                    const qint64 secsLeft = now.secsTo(r.targetTime);
                    const qint64 minsLeft = secsLeft / 60;
                    eventList.append(QStringLiteral("- %1 (Fecha: %2, dentro de %3 minutos)").arg(r.text, timeStr, QString::number(minsLeft)));
                }
            }
            result[QStringLiteral("action")] = QStringLiteral("list");
            result[QStringLiteral("count")] = eventList.size();
            result[QStringLiteral("events")] = eventList.isEmpty() ? QStringLiteral("No hay eventos ni citas próximas programadas.") : eventList.join(QLatin1Char('\n'));
            return result;
        }

        // action == "create"
        QString title = arguments.value(QStringLiteral("title")).toString().trimmed();
        if (title.isEmpty()) title = i18n("Evento en calendario");
        QString timeArg = arguments.value(QStringLiteral("start_time")).toString().trimmed();
        int durationMinutes = arguments.value(QStringLiteral("duration_minutes")).toInt();
        if (durationMinutes <= 0) durationMinutes = 60;
        QString description = arguments.value(QStringLiteral("description")).toString().trimmed();

        ParsedCalendarEvent ev = parseCalendarEventPrompt(title + QStringLiteral(" ") + timeArg, QDateTime::currentDateTime());
        if (!timeArg.isEmpty() && ev.startTime == QDateTime::currentDateTime().addSecs(3600)) {
            QDateTime explicitDt = QDateTime::fromString(timeArg, Qt::ISODate);
            if (!explicitDt.isValid()) {
                explicitDt = QDateTime::fromString(timeArg, QStringLiteral("yyyy-MM-dd HH:mm"));
            }
            if (explicitDt.isValid()) {
                ev.startTime = explicitDt;
                ev.endTime = explicitDt.addSecs(durationMinutes * 60);
            }
        }
        if (!title.isEmpty()) ev.title = title;
        if (!description.isEmpty()) ev.description = description;

        // Generate RFC 5545 iCalendar (.ics) file
        const QString calDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity/calendar");
        QDir().mkpath(calDir);
        const QString eventUid = QStringLiteral("kity_%1").arg(QDateTime::currentMSecsSinceEpoch());
        const QString icsPath = calDir + QStringLiteral("/%1.ics").arg(eventUid);

        QFile icsFile(icsPath);
        if (icsFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&icsFile);
            out << "BEGIN:VCALENDAR\n"
                << "VERSION:2.0\n"
                << "PRODID:-//Kity//KDE Plasma 6.8//ES\n"
                << "BEGIN:VEVENT\n"
                << "UID:" << eventUid << "\n"
                << "DTSTAMP:" << QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'hhmmss'Z'")) << "\n"
                << "DTSTART:" << ev.startTime.toUTC().toString(QStringLiteral("yyyyMMdd'T'hhmmss'Z'")) << "\n"
                << "DTEND:" << ev.endTime.toUTC().toString(QStringLiteral("yyyyMMdd'T'hhmmss'Z'")) << "\n"
                << "SUMMARY:" << ev.title << "\n";
            if (!ev.description.isEmpty()) {
                out << "DESCRIPTION:" << ev.description << "\n";
            }
            out << "END:VEVENT\n"
                << "END:VCALENDAR\n";
            icsFile.close();
        }

        // Open KOrganizer to merge the new event into the default calendar
        const QString korganizer = QStandardPaths::findExecutable(QStringLiteral("korganizer"));
        if (!korganizer.isEmpty()) {
            QProcess::startDetached(korganizer, QStringList() << QStringLiteral("-m") << icsPath);
        }

        // Register in Kity scheduled reminders list
        ScheduledReminder r;
        r.id = eventUid;
        r.text = ev.title;
        r.targetTime = ev.startTime;
        r.notified = false;
        m_scheduledReminders.append(r);
        saveScheduledReminders();

        KNotification::event(QStringLiteral("notification"),
                             i18n("Evento agendado en KOrganizer"),
                             QStringLiteral("%1 (%2)").arg(ev.title, ev.startTime.toString(QStringLiteral("dd/MM/yyyy HH:mm"))),
                             QStringLiteral("x-office-calendar"));

        result[QStringLiteral("action")] = QStringLiteral("create");
        result[QStringLiteral("title")] = ev.title;
        result[QStringLiteral("start_time")] = ev.startTime.toString(Qt::ISODate);
        result[QStringLiteral("ics_path")] = icsPath;
        result[QStringLiteral("status")] = QStringLiteral("Evento «%1» creado y guardado en KOrganizer correctamente para el %2.").arg(ev.title, ev.startTime.toString(QStringLiteral("dddd d 'de' MMMM 'a las' HH:mm")));
        return result;
    }

    if (toolName == QLatin1String("clipboard_tool")) {
        const QString action = arguments.value(QStringLiteral("action")).toString().trimmed().toLower();
        result[QStringLiteral("action")] = action;

        QDBusInterface klipperIface(QStringLiteral("org.kde.klipper"),
                                    QStringLiteral("/klipper"),
                                    QStringLiteral("org.kde.klipper.klipper"),
                                    QDBusConnection::sessionBus());

        if (action == QLatin1String("list_history")) {
            QStringList history;
            if (klipperIface.isValid()) {
                QDBusReply<QStringList> menuReply = klipperIface.call(QStringLiteral("getClipboardHistoryMenu"));
                if (menuReply.isValid() && !menuReply.value().isEmpty()) {
                    history = menuReply.value();
                }
            }
            if (history.isEmpty()) {
                const QString current = readWaylandSelectionOrClipboard();
                if (!current.isEmpty()) {
                    history.append(current);
                }
            }
            result[QStringLiteral("history")] = history.mid(0, 10).join(QStringLiteral("\n---\n"));
            result[QStringLiteral("count")] = history.size();
            return result;
        }

        if (action == QLatin1String("search")) {
            const QString query = arguments.value(QStringLiteral("query")).toString().trimmed();
            QStringList matches;
            if (klipperIface.isValid()) {
                QDBusReply<QStringList> menuReply = klipperIface.call(QStringLiteral("getClipboardHistoryMenu"));
                if (menuReply.isValid()) {
                    for (const QString &item : menuReply.value()) {
                        if (item.contains(query, Qt::CaseInsensitive)) {
                            matches.append(item);
                        }
                    }
                }
            }
            result[QStringLiteral("query")] = query;
            result[QStringLiteral("matches")] = matches.isEmpty() ? QStringLiteral("No se encontraron coincidencias en el historial del portapapeles.") : matches.mid(0, 5).join(QStringLiteral("\n---\n"));
            return result;
        }

        // action == "get_current"
        QString current;
        if (klipperIface.isValid()) {
            QDBusReply<QString> reply = klipperIface.call(QStringLiteral("getClipboardContents"));
            if (reply.isValid()) {
                current = reply.value().trimmed();
            }
        }
        if (current.isEmpty()) {
            current = readWaylandSelectionOrClipboard();
        }
        result[QStringLiteral("clipboard_content")] = current.isEmpty() ? QStringLiteral("(El portapapeles está vacío)") : current;
        return result;
    }

    if (toolName == QLatin1String("email_tool")) {
        const QString to = arguments.value(QStringLiteral("to")).toString().trimmed();
        const QString subject = arguments.value(QStringLiteral("subject")).toString().trimmed();
        const QString body = arguments.value(QStringLiteral("body")).toString().trimmed();

        composeEmail(to, subject, body, QStringLiteral("default"));

        result[QStringLiteral("to")] = to;
        result[QStringLiteral("subject")] = subject;
        result[QStringLiteral("status")] = QStringLiteral("Borrador de correo abierto con éxito en el cliente de correo del sistema.");
        return result;
    }

    result[QStringLiteral("error")] = QStringLiteral("Herramienta no reconocida.");
    return result;
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
    Q_EMIT panelIconStyleChanged();
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
// Voice & Audio Processing (Whisper.cpp + PipeWire Full-Duplex AEC)
// =========================================================================

bool KityApplet::isPipeWireEchoCancelAvailable() const
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

QString KityApplet::resolveAudioRecordingPath() const
{
    QString baseDir = QStringLiteral("/dev/shm");
    if (!QDir(baseDir).exists() || !QFileInfo(baseDir).isWritable()) {
        baseDir = QDir::tempPath();
    }
    return baseDir + QStringLiteral("/kity_rec_%1.wav").arg(QDateTime::currentMSecsSinceEpoch());
}

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

    m_currentRecordingPath = resolveAudioRecordingPath();

    m_recordProcess = new QProcess(this);
    QStringList args;
    args << QStringLiteral("--format=s16")
         << QStringLiteral("--rate=16000")
         << QStringLiteral("--channels=1")
         << QStringLiteral("--latency=20ms")
         << QStringLiteral("--media-category=Capture")
         << QStringLiteral("--media-role=Communication");

    if (isPipeWireEchoCancelAvailable()) {
        args << QStringLiteral("--target=kity_echo_cancelled_source");
    }
    args << m_currentRecordingPath;

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
            // Safety guard: stop after 4.5 seconds if in conversational follow-up, or 7 seconds initially
            const double maxSilenceSeconds = (m_lastQueryWasVoice ? 4.5 : 7.0);
            if (fileSize > static_cast<qint64>(16000 * sizeof(qint16) * maxSilenceSeconds)) {
                stopRecording();
                setState(QStringLiteral("idle"), QString());
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
         << QStringLiteral("-t") << QStringLiteral("6")
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
    m_bargeInVoiceFrames = 0;

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

    // Regex 1: Full sentence terminators (. ? ! \n)
    static const QRegularExpression sentenceTerminator(QStringLiteral("([.?!\\n]+)"));
    // Regex 2: Clause pause boundaries (, ; : —) for sub-sentence low latency streaming
    static const QRegularExpression clauseTerminator(QStringLiteral("([,;:—]+)"));

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
        // This achieves ultra-low Time-To-First-Speech (< 350ms) like Gemini Live!
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
            const bool continuousVoice = readConfigBool(QStringLiteral("continuousConversation"), true);
            if (m_lastQueryWasVoice && continuousVoice) {
                qInfo() << "Kity: Full-Duplex continuous conversational turn-taking active. Listening for natural follow-up...";
                setState(QStringLiteral("listening"), i18n("Escuchando respuesta…"));
                startRecording();
                return;
            } else {
                setState(QStringLiteral("idle"), QString());
            }
        }
        return;
    }

    const QString sentence = m_ttsQueue.dequeue();
    speakSentence(sentence);
}

void KityApplet::speakSentence(const QString &sentence)
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

    // 1. Google Cloud Text-to-Speech API (Neural2 / Journey / Studio voices)
    if (ttsEngine == QLatin1String("google") && !googleApiKey.isEmpty()) {
        m_isSpeakingSentence = true;
        setState(QStringLiteral("speaking"), i18n("Reproduciendo respuesta…"));

        if (m_wakeWordEnabled && (!m_wakeWordProcess || m_wakeWordProcess->state() != QProcess::Running)) {
            startWakeWordListener();
        }

        const QString googleVoice = readConfigString(QStringLiteral("googleVoice"), QStringLiteral("es-ES-Neural2-F"));
        QJsonObject inputObj;
        inputObj[QStringLiteral("text")] = sentence;

        QJsonObject voiceObj;
        voiceObj[QStringLiteral("languageCode")] = QStringLiteral("es-ES");
        voiceObj[QStringLiteral("name")] = googleVoice;
        voiceObj[QStringLiteral("ssmlGender")] = QStringLiteral("FEMALE");

        QJsonObject audioConfig;
        audioConfig[QStringLiteral("audioEncoding")] = QStringLiteral("MP3");

        QJsonObject payload;
        payload[QStringLiteral("input")] = inputObj;
        payload[QStringLiteral("voice")] = voiceObj;
        payload[QStringLiteral("audioConfig")] = audioConfig;

        const QUrl gUrl(QStringLiteral("https://texttospeech.googleapis.com/v1/text:synthesize?key=%1").arg(googleApiKey));
        QNetworkRequest req(gUrl);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

        QNetworkReply *gReply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        connect(gReply, &QNetworkReply::finished, this, [this, gReply, pwPlay]() {
            gReply->deleteLater();
            if (gReply->error() != QNetworkReply::NoError) {
                qWarning() << "Kity: Google Cloud TTS error:" << gReply->errorString();
                setState(QStringLiteral("error"), i18n("Error en la síntesis de voz (Google Cloud TTS): %1", gReply->errorString()));
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
        setState(QStringLiteral("speaking"), i18n("Reproduciendo respuesta…"));

        if (m_wakeWordEnabled && (!m_wakeWordProcess || m_wakeWordProcess->state() != QProcess::Running)) {
            startWakeWordListener();
        }

        const QString edgeVoice = readConfigString(QStringLiteral("edgeVoice"), QStringLiteral("es-ES-ElviraNeural"));
        m_ttsProcess = new QProcess(this);
        m_pwPlayProcess = new QProcess(this);

        m_ttsProcess->setStandardOutputProcess(m_pwPlayProcess.data());

        QStringList edgeArgs;
        edgeArgs << QStringLiteral("--voice") << edgeVoice
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
        execArgs << QStringLiteral("--model") << piperVoicePath
                 << QStringLiteral("--output_file") << QStringLiteral("-");
    } else if (!kokoroCli.isEmpty()) {
        execProg = kokoroCli;
        execArgs << QStringLiteral("--voice") << QStringLiteral("ef_dora")
                 << QStringLiteral("--output_file") << QStringLiteral("-");
    } else {
        m_isSpeakingSentence = false;
        setState(QStringLiteral("error"), i18n("No se encontró ningún motor TTS disponible (Google, Edge, Piper o Kokoro)."));
        playNextTtsQueueItem();
        return;
    }

    m_isSpeakingSentence = true;
    setState(QStringLiteral("speaking"), i18n("Reproduciendo respuesta…"));

    if (m_wakeWordEnabled && (!m_wakeWordProcess || m_wakeWordProcess->state() != QProcess::Running)) {
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

    if (m_state == QLatin1String("listening")) {
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
    m_bargeInVoiceFrames = 0;

    if (!m_wakeWordProcess) {
        m_wakeWordProcess = new QProcess(this);
        connect(m_wakeWordProcess, &QProcess::readyReadStandardOutput, this, &KityApplet::onWakeWordAudioChunkReady);
    }

    QStringList args;
    args << QStringLiteral("--format=s16")
         << QStringLiteral("--rate=16000")
         << QStringLiteral("--channels=1")
         << QStringLiteral("--container=raw")
         << QStringLiteral("--latency=20ms")
         << QStringLiteral("--media-category=Capture")
         << QStringLiteral("--media-role=Communication");

    if (isPipeWireEchoCancelAvailable()) {
        args << QStringLiteral("--target=kity_echo_cancelled_source");
    }
    args << QStringLiteral("-");

    m_wakeWordProcess->start(pwRecord, args);
    if (!m_wakeWordProcess->waitForStarted(2000)) {
        qWarning() << "Kity: Failed to start pw-record for wake word listener";
    } else {
        qInfo() << "Kity: Wake word & Barge-in background listener started successfully (PID:" << m_wakeWordProcess->processId() << ")";
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
    m_bargeInVoiceFrames = 0;
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

    // Live Full-Duplex Barge-in detection: if Kity is speaking and user begins speaking directly
    if (m_state == QLatin1String("speaking")) {
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
         << QStringLiteral("-t") << QStringLiteral("6")
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

    // 4. Media Playback Control via playerctl & Generative MediaCard
    static const QRegularExpression mediaQueryRegex(QStringLiteral("(?:qu[eé]\\s+(?:est[aá]\\s+)?sonando|qu[eé]\\s+canci[oó]n\\s+es|m[uú]sica\\s+actual|pista\\s+actual)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(mediaQueryRegex)) {
        const QVariantMap activeMedia = queryActiveMedia();
        if (!activeMedia.isEmpty()) {
            m_pendingCardType = QStringLiteral("media");
            m_pendingCardData = activeMedia;
            const QString resp = i18n("Actualmente suena: %1 (%2).", activeMedia.value(QStringLiteral("title")).toString(), activeMedia.value(QStringLiteral("artist")).toString());
            appendAssistantMessage(resp);
            return true;
        } else {
            appendAssistantMessage(i18n("No hay ningún reproductor multimedia activo en este momento."));
            return true;
        }
    }

    static const QRegularExpression mediaPlayRegex(QStringLiteral("(?:reproduce|reanuda|play)\\s+(?:la\\s+)?(?:m[uú]sica|canci[oó]n|audio)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(mediaPlayRegex) || trimmed.compare(QStringLiteral("reproducir"), Qt::CaseInsensitive) == 0) {
        QProcess::execute(QStringLiteral("playerctl"), QStringList() << QStringLiteral("play"));
        const QVariantMap activeMedia = queryActiveMedia();
        if (!activeMedia.isEmpty()) {
            m_pendingCardType = QStringLiteral("media");
            m_pendingCardData = activeMedia;
        }
        const QString resp = i18n("Reanudando reproducción.");
        appendAssistantMessage(resp);
        return true;
    }

    static const QRegularExpression mediaPauseRegex(QStringLiteral("(?:pausa|pausar|para|parar)\\s+(?:la\\s+)?(?:m[uú]sica|canci[oó]n|audio)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(mediaPauseRegex) || trimmed.compare(QStringLiteral("pausa"), Qt::CaseInsensitive) == 0) {
        QProcess::execute(QStringLiteral("playerctl"), QStringList() << QStringLiteral("pause"));
        const QVariantMap activeMedia = queryActiveMedia();
        if (!activeMedia.isEmpty()) {
            m_pendingCardType = QStringLiteral("media");
            m_pendingCardData = activeMedia;
        }
        const QString resp = i18n("Pausando reproducción.");
        appendAssistantMessage(resp);
        return true;
    }

    static const QRegularExpression mediaNextRegex(QStringLiteral("(?:siguiente|pasa\\s+de)\\s+(?:canci[oó]n|pista)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(mediaNextRegex)) {
        QProcess::execute(QStringLiteral("playerctl"), QStringList() << QStringLiteral("next"));
        const QVariantMap activeMedia = queryActiveMedia();
        if (!activeMedia.isEmpty()) {
            m_pendingCardType = QStringLiteral("media");
            m_pendingCardData = activeMedia;
        }
        const QString resp = i18n("Siguiente pista.");
        appendAssistantMessage(resp);
        return true;
    }

    static const QRegularExpression mediaPrevRegex(QStringLiteral("(?:anterior|atr[aá]s)\\s+(?:canci[oó]n|pista)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(mediaPrevRegex)) {
        QProcess::execute(QStringLiteral("playerctl"), QStringList() << QStringLiteral("previous"));
        const QVariantMap activeMedia = queryActiveMedia();
        if (!activeMedia.isEmpty()) {
            m_pendingCardType = QStringLiteral("media");
            m_pendingCardData = activeMedia;
        }
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

    // 13. Calendar Events: "crea/añade/pon/agenda/programa evento/cita/reunión ..."
    static const QRegularExpression calRegex(
        QStringLiteral("(?:crea|a[ñn]ade|pon|agenda|programa)\\s+(?:un\\s+|una\\s+)?(?:evento|cita|reuni[oó]n)\\s+(.+)"),
        QRegularExpression::CaseInsensitiveOption);
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
    // Try primary Wayland selection first via wl-paste (works globally across unfocused clients)
    QProcess process;
    process.start(QStringLiteral("wl-paste"), {QStringLiteral("-p"), QStringLiteral("--no-newline")});
    if (process.waitForFinished(800)) {
        const QString text = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        if (!text.isEmpty()) {
            return text;
        }
    }

    // Try standard Wayland clipboard
    process.start(QStringLiteral("wl-paste"), {QStringLiteral("--no-newline")});
    if (process.waitForFinished(800)) {
        const QString text = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        if (!text.isEmpty()) {
            return text;
        }
    }

    // Fallback to Qt clipboard
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
            "Eres un traductor experto. Traduce el siguiente texto a %1 de forma completamente natural y fiel, "
            "detectando automáticamente el idioma de origen. Devuelve únicamente la traducción, sin notas ni comentarios.").arg(target);
    } else if (action == QStringLiteral("proofread")) {
        instruction = QStringLiteral(
            "Eres un corrector de estilo y ortografía profesional. "
            "Corrige la ortografía, gramática y puntuación del siguiente texto en español de España, manteniendo su significado y estilo original. "
            "Devuelve directamente el texto corregido listo para copiar y, debajo, una breve nota de 1 o 2 líneas resumiendo las mejoras aplicadas.");
    } else if (action == QStringLiteral("rewrite")) {
        instruction = QStringLiteral(
            "Eres un redactor profesional. "
            "Reescribe el siguiente texto mejorando su fluidez, claridad, vocabulario y elegancia en español de España. "
            "Devuelve únicamente la versión reescrita lista para copiar, sin preámbulos.");
    } else if (action == QStringLiteral("friendly")) {
        instruction = QStringLiteral(
            "Eres un redactor experto en comunicación cercana. "
            "Reescribe el siguiente texto con un tono cercano, cálido y amigable (Friendly tone) en español de España. "
            "Devuelve únicamente el texto resultante listo para usar.");
    } else if (action == QStringLiteral("professional")) {
        instruction = QStringLiteral(
            "Eres un redactor corporativo de alto nivel. "
            "Reescribe el siguiente texto con un tono formal, ejecutivo y profesional (Professional tone) en español de España. "
            "Devuelve únicamente el texto resultante listo para usar.");
    } else if (action == QStringLiteral("concise")) {
        instruction = QStringLiteral(
            "Eres un editor de síntesis. "
            "Reescribe el siguiente texto de forma concisa, directa y breve (Concise tone), eliminando cualquier redundancia. "
            "Devuelve únicamente el texto resultante listo para usar.");
    } else if (action == QStringLiteral("summary")) {
        instruction = QStringLiteral(
            "Eres un especialista en síntesis de información. "
            "Genera un resumen ejecutivo claro y bien estructurado del siguiente texto en español de España.");
    } else if (action == QStringLiteral("keypoints")) {
        instruction = QStringLiteral(
            "Eres un analista de información. "
            "Extrae los puntos clave (Key Points) del siguiente texto en una lista con viñetas clara, estructurada y directa.");
    } else if (action == QStringLiteral("table")) {
        instruction = QStringLiteral(
            "Eres un organizador de información estructurada. "
            "Organiza y estructura todos los datos e información del siguiente texto en una tabla Markdown clara y elegante.");
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

    const QString aiBackend = readConfigString(QStringLiteral("aiBackend"), QStringLiteral("hybrid")).toLower();
    const QString geminiApiKey = resolveGeminiApiKey();
    const bool useGemini = (aiBackend == QLatin1String("gemini") || (aiBackend == QLatin1String("hybrid") && !geminiApiKey.isEmpty()));

    if (useGemini) {
        sendToGemini(fullPrompt);
    } else {
        sendToOllama(fullPrompt);
    }
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
        setState(QStringLiteral("idle"), i18n("Captura de pantalla cancelada o no disponible."));
        return;
    }

    setState(QStringLiteral("thinking"), i18n("Extrayendo texto e información de la pantalla…"));
    QCoreApplication::processEvents();

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
    vitals << QStringLiteral("SO: CachyOS Linux (KDE Plasma 6, Wayland)");

    // RAM stats from /proc/meminfo
    QFile meminfo(QStringLiteral("/proc/meminfo"));
    if (meminfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qint64 totalKb = 0;
        qint64 availKb = 0;
        while (!meminfo.atEnd()) {
            const QString line = QString::fromUtf8(meminfo.readLine());
            if (line.startsWith(QLatin1String("MemTotal:"))) {
                totalKb = line.split(QRegularExpression(QStringLiteral("\\s+"))).value(1).toLongLong();
            } else if (line.startsWith(QLatin1String("MemAvailable:"))) {
                availKb = line.split(QRegularExpression(QStringLiteral("\\s+"))).value(1).toLongLong();
            }
            if (totalKb > 0 && availKb > 0) break;
        }
        if (totalKb > 0) {
            const double totalGb = totalKb / (1024.0 * 1024.0);
            const double availGb = availKb / (1024.0 * 1024.0);
            vitals << QStringLiteral("RAM: %1 GB libres de %2 GB").arg(availGb, 0, 'f', 1).arg(totalGb, 0, 'f', 1);
        }
    }

    // CPU load average from /proc/loadavg
    QFile loadavg(QStringLiteral("/proc/loadavg"));
    if (loadavg.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString lLine = QString::fromUtf8(loadavg.readLine()).trimmed();
        const QStringList parts = lLine.split(QLatin1Char(' '));
        if (!parts.isEmpty()) {
            vitals << QStringLiteral("Carga CPU (1 min): %1").arg(parts.at(0));
        }
    }

    const QString pProfile = getActivePowerProfile();
    if (!pProfile.isEmpty()) {
        vitals << QStringLiteral("Perfil energía: %1").arg(pProfile);
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

QString KityApplet::wmoCodeToSpanish(int code)
{
    switch (code) {
    case 0: return QStringLiteral("despejado");
    case 1: return QStringLiteral("mayormente despejado");
    case 2: return QStringLiteral("parcialmente nuboso");
    case 3: return QStringLiteral("nublado");
    case 45: case 48: return QStringLiteral("niebla");
    case 51: case 53: case 55: return QStringLiteral("llovizna");
    case 61: case 63: case 65: return QStringLiteral("lluvia");
    case 66: case 67: return QStringLiteral("lluvia helada");
    case 71: case 73: case 75: return QStringLiteral("nieve");
    case 77: return QStringLiteral("granizo fino");
    case 80: case 81: case 82: return QStringLiteral("chubascos");
    case 85: case 86: return QStringLiteral("chubascos de nieve");
    case 95: return QStringLiteral("tormenta");
    case 96: case 99: return QStringLiteral("tormenta con granizo");
    default: return QStringLiteral("variable");
    }
}

QString KityApplet::extractCityFromQuery(const QString &query) const
{
    static const QRegularExpression cityRegex(
        QStringLiteral("(?:en|de|para|sobre)\\s+([A-ZÁÉÍÓÚa-záéíóúñÑçÇ\\s\\-\\.]+?)(?:\\s+(?:hoy|ahora(?:\\s+mismo)?|mañana|esta\\s+tarde|esta\\s+noche|el\\s+fin\\s+de\\s+semana|por\\s+favor)|[\\?\\!\\.,;]*$)"),
        QRegularExpression::CaseInsensitiveOption);

    const auto match = cityRegex.match(query);
    if (match.hasMatch()) {
        QString city = match.captured(1).trimmed();
        city.remove(QRegularExpression(QStringLiteral("[\\?\\!\\.,;]+$")));
        if (!city.isEmpty()) {
            return city.trimmed();
        }
    }
    return QString();
}

QString KityApplet::resolveDefaultLocation() const
{
    const QString customCity = readConfigString(QStringLiteral("defaultCity"), QString());
    if (!customCity.isEmpty()) {
        return customCity;
    }

    // Try detecting city from Plasma weather widgets in desktop-appletsrc
    QFile f(QDir::homePath() + QStringLiteral("/.config/plasma-org.kde.plasma.desktop-appletsrc"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (line.startsWith(QStringLiteral("locationName="))) {
                const QString loc = line.mid(13).trimmed();
                if (!loc.isEmpty()) {
                    const QString first = loc.section(QLatin1Char(','), 0, 0).trimmed();
                    if (!first.isEmpty()) {
                        if (first.contains(QStringLiteral("Calzada"), Qt::CaseInsensitive)) {
                            return QStringLiteral("Gijón");
                        }
                        return first;
                    }
                }
            }
        }
    }

    return QStringLiteral("Gijón");
}

QVariantMap KityApplet::fetchWeatherData(const QString &query) const
{
    QString targetCity = extractCityFromQuery(query);
    if (targetCity.isEmpty()) {
        targetCity = resolveDefaultLocation();
    }

    const QUrl geoUrl(QStringLiteral("https://geocoding-api.open-meteo.com/v1/search?name=%1&count=1&language=es&format=json")
                          .arg(QUrl::toPercentEncoding(targetCity)));
    const QByteArray geoResp = httpGetSync(geoUrl, 2500);
    if (geoResp.isEmpty()) {
        return QVariantMap();
    }

    const QJsonDocument geoDoc = QJsonDocument::fromJson(geoResp);
    const QJsonArray results = geoDoc.object().value(QStringLiteral("results")).toArray();
    if (results.isEmpty()) {
        return QVariantMap();
    }

    const QJsonObject locObj = results.first().toObject();
    const double lat = locObj.value(QStringLiteral("latitude")).toDouble();
    const double lon = locObj.value(QStringLiteral("longitude")).toDouble();
    const QString resolvedName = locObj.value(QStringLiteral("name")).toString();
    const QString admin = locObj.value(QStringLiteral("admin1")).toString();
    const QString country = locObj.value(QStringLiteral("country")).toString();

    const QUrl weatherUrl(QStringLiteral("https://api.open-meteo.com/v1/forecast?latitude=%1&longitude=%2&current=temperature_2m,relative_humidity_2m,apparent_temperature,precipitation,weather_code,wind_speed_10m&timezone=auto")
                              .arg(lat, 0, 'f', 4)
                              .arg(lon, 0, 'f', 4));
    const QByteArray weatherResp = httpGetSync(weatherUrl, 2500);
    if (weatherResp.isEmpty()) {
        return QVariantMap();
    }

    const QJsonDocument wDoc = QJsonDocument::fromJson(weatherResp);
    const QJsonObject current = wDoc.object().value(QStringLiteral("current")).toObject();
    if (current.isEmpty()) {
        return QVariantMap();
    }

    const double temp = current.value(QStringLiteral("temperature_2m")).toDouble();
    const double apparent = current.value(QStringLiteral("apparent_temperature")).toDouble();
    const int wmoCode = current.value(QStringLiteral("weather_code")).toInt();
    const double wind = current.value(QStringLiteral("wind_speed_10m")).toDouble();
    const double humidity = current.value(QStringLiteral("relative_humidity_2m")).toDouble();
    const double precip = current.value(QStringLiteral("precipitation")).toDouble();

    const QString condition = wmoCodeToSpanish(wmoCode);
    QString fullLocation = resolvedName;
    if (!admin.isEmpty() && admin != resolvedName) {
        fullLocation.append(QStringLiteral(", %1").arg(admin));
    }
    if (!country.isEmpty()) {
        fullLocation.append(QStringLiteral(" (%1)").arg(country));
    }

    QString iconName = QStringLiteral("weather-few-clouds");
    if (wmoCode == 0) iconName = QStringLiteral("weather-clear");
    else if (wmoCode == 1) iconName = QStringLiteral("weather-few-clouds");
    else if (wmoCode == 2) iconName = QStringLiteral("weather-clouds");
    else if (wmoCode == 3) iconName = QStringLiteral("weather-overcast");
    else if (wmoCode == 45 || wmoCode == 48) iconName = QStringLiteral("weather-fog");
    else if (wmoCode >= 51 && wmoCode <= 55) iconName = QStringLiteral("weather-showers-scattered");
    else if (wmoCode >= 61 && wmoCode <= 65) iconName = QStringLiteral("weather-showers");
    else if (wmoCode >= 66 && wmoCode <= 67) iconName = QStringLiteral("weather-freezing-rain");
    else if (wmoCode >= 71 && wmoCode <= 77) iconName = QStringLiteral("weather-snow");
    else if (wmoCode >= 80 && wmoCode <= 82) iconName = QStringLiteral("weather-storm");
    else if (wmoCode >= 85 && wmoCode <= 86) iconName = QStringLiteral("weather-snow-rain");
    else if (wmoCode >= 95) iconName = QStringLiteral("weather-severe-alert");

    QVariantMap data;
    data[QStringLiteral("location")] = fullLocation;
    data[QStringLiteral("city")] = resolvedName;
    data[QStringLiteral("temp")] = temp;
    data[QStringLiteral("feelsLike")] = apparent;
    data[QStringLiteral("condition")] = condition;
    data[QStringLiteral("humidity")] = humidity;
    data[QStringLiteral("wind")] = wind;
    data[QStringLiteral("precipitation")] = precip;
    data[QStringLiteral("wmoCode")] = wmoCode;
    data[QStringLiteral("iconName")] = iconName;
    return data;
}

QString KityApplet::fetchWeatherContext(const QString &query) const
{
    static const QRegularExpression weatherRegex(
        QStringLiteral("(tiempo|clima|temperatura|llueve|llover|lluvia|fr[ií]o|calor|grados|pron[oó]stico|viento|soleado|nublado)"),
        QRegularExpression::CaseInsensitiveOption);
    if (!query.contains(weatherRegex)) {
        return QString();
    }

    const QVariantMap data = fetchWeatherData(query);
    if (data.isEmpty()) {
        return QString();
    }

    // Attach WeatherCard to pending generative UI
    const_cast<KityApplet*>(this)->m_pendingCardType = QStringLiteral("weather");
    const_cast<KityApplet*>(this)->m_pendingCardData = data;

    const QString fullLocation = data.value(QStringLiteral("location")).toString();
    const QString resolvedName = data.value(QStringLiteral("city")).toString();
    const double temp = data.value(QStringLiteral("temp")).toDouble();
    const double apparent = data.value(QStringLiteral("feelsLike")).toDouble();
    const QString condition = data.value(QStringLiteral("condition")).toString();
    const double humidity = data.value(QStringLiteral("humidity")).toDouble();
    const double wind = data.value(QStringLiteral("wind")).toDouble();
    const double precip = data.value(QStringLiteral("precipitation")).toDouble();

    return QStringLiteral(
                        "[Datos meteorológicos oficiales y actualizados en tiempo real para %1:\n"
                        "- Temperatura actual: %2 °C\n"
                        "- Sensación térmica: %3 °C\n"
                        "- Estado del cielo: %4\n"
                        "- Humedad relativa: %5 %\n"
                        "- Viento: %6 km/h\n"
                        "- Precipitaciones: %7 mm\n"
                        "INSTRUCCIÓN OBLIGATORIA: Eres Kity, el asistente de voz. Responde a la pregunta del usuario indicando claramente que en %8 hace %2 °C con cielo %4. Tu respuesta debe ser concisa, directa y natural en una sola frase optimizada para hablar por voz al estilo Siri. No digas que no tienes acceso a información en tiempo real.]")
                        .arg(fullLocation)
                        .arg(QString::number(temp, 'f', 1))
                        .arg(QString::number(apparent, 'f', 1))
                        .arg(condition)
                        .arg(QString::number(humidity, 'f', 0))
                        .arg(QString::number(wind, 'f', 1))
                        .arg(QString::number(precip, 'f', 1))
                        .arg(resolvedName);
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

QString KityApplet::extractCleanArticleText(const QString &html) const
{
    if (html.isEmpty()) {
        return QString();
    }

    QString text = html;

    // 1. Strip script, style, svg, iframe, noscript, nav, header, footer, aside blocks
    static const QRegularExpression stripBlockRegex(
        QStringLiteral("<(script|style|svg|iframe|noscript|nav|header|footer|aside)[^>]*>.*?</\\1>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    text.remove(stripBlockRegex);

    // 2. Convert block boundaries to newlines
    static const QRegularExpression blockBreaks(
        QStringLiteral("<(?:br|p|div|li|h[1-6]|tr)[^>]*>"),
        QRegularExpression::CaseInsensitiveOption);
    text.replace(blockBreaks, QStringLiteral("\n"));

    // 3. Remove all remaining HTML tags
    static const QRegularExpression anyTag(QStringLiteral("<[^>]+>"));
    text.remove(anyTag);

    // 4. Decode HTML entities
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""))
        .replace(QStringLiteral("&#x27;"), QStringLiteral("'"))
        .replace(QStringLiteral("&#39;"), QStringLiteral("'"))
        .replace(QStringLiteral("&amp;"), QStringLiteral("&"))
        .replace(QStringLiteral("&lt;"), QStringLiteral("<"))
        .replace(QStringLiteral("&gt;"), QStringLiteral(">"))
        .replace(QStringLiteral("&nbsp;"), QStringLiteral(" "))
        .replace(QStringLiteral("&#160;"), QStringLiteral(" "));

    // 5. Keep only substantive paragraphs (>= 35 characters)
    const QStringList rawLines = text.split(QLatin1Char('\n'));
    QStringList substantiveLines;
    for (const QString &line : rawLines) {
        const QString trimmed = line.simplified();
        if (trimmed.length() >= 35) {
            substantiveLines.append(trimmed);
        }
    }

    QString result = substantiveLines.join(QStringLiteral("\n\n"));
    if (result.length() > 2500) {
        result = result.left(2500) + QStringLiteral("…");
    }
    return result;
}

QString KityApplet::fetchDeepWebPageContent(const QUrl &url) const
{
    if (!url.isValid() || url.scheme().isEmpty()) {
        return QString();
    }
    const QByteArray htmlBytes = httpGetSync(url, 2800);
    if (htmlBytes.isEmpty()) {
        return QString();
    }
    return extractCleanArticleText(QString::fromUtf8(htmlBytes));
}

QString KityApplet::fetchWebSearchContext(const QString &query) const
{
    QString searchTerm = query.trimmed();
    static const QRegularExpression prefixStrip(
        QStringLiteral("^(?:busca(?:r)?|averigua(?:r)?|qui[eé]n es|qu[eé] es|dime|encuentra(?:r)?|consulta(?:r)?)\\s+"),
        QRegularExpression::CaseInsensitiveOption);
    searchTerm.remove(prefixStrip);
    searchTerm = searchTerm.trimmed();
    if (searchTerm.isEmpty()) {
        return QString();
    }

    struct SearchResultItem {
        QString title;
        QString url;
        QString snippet;
        QString deepContent;
    };
    QList<SearchResultItem> foundItems;

    // 1. DuckDuckGo HTML Search via POST (reliable, real-time web results with links & snippets)
    const QUrl ddgUrl(QStringLiteral("https://html.duckduckgo.com/html/"));
    const QByteArray formData = "q=" + QUrl::toPercentEncoding(searchTerm);
    const QByteArray ddgResp = httpPostFormSync(ddgUrl, formData, 3500);

    if (!ddgResp.isEmpty()) {
        const QString html = QString::fromUtf8(ddgResp);

        static const QRegularExpression titleLinkRx(
            QStringLiteral("<h2 class=\"result__title\">\\s*<a[^>]*class=\"result__a\"[^>]*href=\"([^\"]+)\"[^>]*>(.*?)</a>"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression snippetRx(
            QStringLiteral("<a class=\"result__snippet\"[^>]*>(.*?)</a>"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression tagRx(QStringLiteral("<[^>]+>"));

        auto titleIt = titleLinkRx.globalMatch(html);
        auto snippetIt = snippetRx.globalMatch(html);

        while (titleIt.hasNext() && foundItems.size() < 4) {
            auto titleMatch = titleIt.next();
            QString rawUrl = titleMatch.captured(1);
            QString rawTitle = titleMatch.captured(2);

            // Clean title
            rawTitle.remove(tagRx);
            rawTitle.replace(QStringLiteral("&quot;"), QStringLiteral("\""))
                    .replace(QStringLiteral("&#x27;"), QStringLiteral("'"))
                    .replace(QStringLiteral("&amp;"), QStringLiteral("&"))
                    .replace(QStringLiteral("&lt;"), QStringLiteral("<"))
                    .replace(QStringLiteral("&gt;"), QStringLiteral(">"));
            const QString title = rawTitle.simplified().trimmed();

            // Decode URL if it was wrapped in /l/?uddg=
            QString cleanUrl = rawUrl;
            if (rawUrl.contains(QStringLiteral("uddg="))) {
                QUrl parsed(rawUrl.startsWith(QLatin1String("//")) ? QStringLiteral("https:") + rawUrl : rawUrl);
                const QString uddg = QUrlQuery(parsed).queryItemValue(QStringLiteral("uddg"));
                if (!uddg.isEmpty()) {
                    cleanUrl = QUrl::fromPercentEncoding(uddg.toUtf8());
                }
            } else if (rawUrl.startsWith(QLatin1String("//"))) {
                cleanUrl = QStringLiteral("https:") + rawUrl;
            }

            // Extract snippet
            QString snippet;
            if (snippetIt.hasNext()) {
                snippet = snippetIt.next().captured(1);
                snippet.remove(tagRx);
                snippet.replace(QStringLiteral("&quot;"), QStringLiteral("\""))
                       .replace(QStringLiteral("&#x27;"), QStringLiteral("'"))
                       .replace(QStringLiteral("&amp;"), QStringLiteral("&"))
                       .replace(QStringLiteral("&lt;"), QStringLiteral("<"))
                       .replace(QStringLiteral("&gt;"), QStringLiteral(">"));
                snippet = snippet.simplified().trimmed();
            }

            if (!cleanUrl.isEmpty() && !title.isEmpty()) {
                SearchResultItem item;
                item.title = title;
                item.url = cleanUrl;
                item.snippet = snippet;
                foundItems.append(item);
            }
        }
    }

    // 2. Fallback: Wikipedia OpenSearch API
    if (foundItems.isEmpty()) {
        const QUrl wikiUrl(QStringLiteral("https://es.wikipedia.org/w/api.php?action=opensearch&search=%1&limit=2&namespace=0&format=json")
                               .arg(QString::fromUtf8(QUrl::toPercentEncoding(searchTerm))));
        const QByteArray wikiResp = httpGetSync(wikiUrl, 2500);
        if (!wikiResp.isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(wikiResp);
            if (doc.isArray()) {
                const QJsonArray arr = doc.array();
                if (arr.size() >= 4) {
                    const QJsonArray titles = arr.at(1).toArray();
                    const QJsonArray descs = arr.at(2).toArray();
                    const QJsonArray links = arr.at(3).toArray();
                    for (int i = 0; i < titles.size(); ++i) {
                        const QString t = titles.at(i).toString();
                        const QString d = descs.at(i).toString();
                        const QString l = links.at(i).toString();
                        if (!d.isEmpty()) {
                            SearchResultItem item;
                            item.title = t;
                            item.url = l;
                            item.snippet = d;
                            foundItems.append(item);
                        }
                    }
                }
            }
        }
    }

    if (foundItems.isEmpty()) {
        return QString();
    }

    // 3. Deep Web Research: Fetch actual page body for the top 2 authoritative sources
    for (int i = 0; i < qMin(foundItems.size(), 2); ++i) {
        const QString deepText = fetchDeepWebPageContent(QUrl(foundItems[i].url));
        if (deepText.length() >= 200) {
            foundItems[i].deepContent = deepText;
        }
    }

    // 4. Format structured research context with citations
    QStringList formattedSources;
    QStringList sourcesListForFooter;
    int idx = 1;
    for (const auto &item : foundItems) {
        if (!item.deepContent.isEmpty()) {
            formattedSources.append(QStringLiteral(
                "Fuente [%1]: \"%2\"\n"
                "Enlace: %3\n"
                "Extracto completo del artículo:\n\"\"\"\n%4\n\"\"\"")
                .arg(QString::number(idx), item.title, item.url, item.deepContent));
        } else {
            formattedSources.append(QStringLiteral(
                "Fuente [%1]: \"%2\"\n"
                "Enlace: %3\n"
                "Resumen verificado: %4")
                .arg(QString::number(idx), item.title, item.url, item.snippet));
        }
        sourcesListForFooter.append(QStringLiteral("- [%1](%2)").arg(item.title, item.url));
        idx++;
    }

    return QStringLiteral(
        "[INVESTIGACIÓN WEB EN TIEMPO REAL (DEEP WEB RESEARCH) PARA: \"%1\"]\n\n"
        "%2\n\n"
        "INSTRUCCIONES OBLIGATORIAS PARA KITY:\n"
        "- Responde en español de España de forma concisa, analítica, natural y directa utilizando la información extraída de estas páginas.\n"
        "- Si mencionas un dato relevante, referencia su fuente entre corchetes con enlace Markdown (por ejemplo: [%3](%4) o [1](%4)).\n"
        "- Al final de tu respuesta, añade OBLIGATORIAMENTE este apartado de fuentes consultadas:\n\n"
        "🌐 **Fuentes consultadas:**\n%5")
        .arg(searchTerm, formattedSources.join(QStringLiteral("\n\n")), foundItems.first().title, foundItems.first().url, sourcesListForFooter.join(QLatin1Char('\n')));
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

    // Generate RFC 5545 iCalendar (.ics) file
    const QString calDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity/calendar");
    QDir().mkpath(calDir);
    const QString eventUid = QStringLiteral("kity_%1").arg(QDateTime::currentMSecsSinceEpoch());
    const QString icsPath = calDir + QStringLiteral("/%1.ics").arg(eventUid);

    QFile icsFile(icsPath);
    if (icsFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&icsFile);
        out << "BEGIN:VCALENDAR\n"
            << "VERSION:2.0\n"
            << "PRODID:-//Kity//KDE Plasma 6.8//ES\n"
            << "BEGIN:VEVENT\n"
            << "UID:" << eventUid << "\n"
            << "DTSTAMP:" << QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'hhmmss'Z'")) << "\n"
            << "DTSTART:" << ev.startTime.toUTC().toString(QStringLiteral("yyyyMMdd'T'hhmmss'Z'")) << "\n"
            << "DTEND:" << ev.endTime.toUTC().toString(QStringLiteral("yyyyMMdd'T'hhmmss'Z'")) << "\n"
            << "SUMMARY:" << ev.title << "\n";
        if (!ev.description.isEmpty()) {
            out << "DESCRIPTION:" << ev.description << "\n";
        }
        out << "END:VEVENT\n"
            << "END:VCALENDAR\n";
        icsFile.close();
    }

    // Import event into KOrganizer if client is korganizer
    const QString client = readConfigString(QStringLiteral("defaultCalendarClient"), QStringLiteral("korganizer")).toLower();
    if (client == QStringLiteral("korganizer")) {
        const QString korganizer = QStandardPaths::findExecutable(QStringLiteral("korganizer"));
        if (!korganizer.isEmpty()) {
            QProcess::startDetached(korganizer, QStringList() << QStringLiteral("-m") << icsPath);
        }
    }

    // Trigger Akonadi synchronization for Google Calendar and other resources
    QDBusInterface agentMgr(QStringLiteral("org.freedesktop.Akonadi.Control"),
                            QStringLiteral("/AgentManager"),
                            QStringLiteral("org.freedesktop.Akonadi.AgentManager"),
                            QDBusConnection::sessionBus());
    if (agentMgr.isValid()) {
        const QDBusReply<QStringList> reply = agentMgr.call(QStringLiteral("agentInstances"));
        if (reply.isValid()) {
            for (const QString &inst : reply.value()) {
                if (inst.startsWith(QStringLiteral("akonadi_google_resource")) || inst.startsWith(QStringLiteral("akonadi_ical_resource"))) {
                    agentMgr.call(QStringLiteral("agentInstanceSynchronize"), inst);
                }
            }
        }
    }

    ScheduledReminder r;
    r.id = eventUid;
    r.text = ev.title;
    r.targetTime = ev.startTime;
    r.notified = false;
    m_scheduledReminders.append(r);
    saveScheduledReminders();

    // Prepare generative UI EventCard
    QVariantMap eventCard;
    eventCard[QStringLiteral("title")] = ev.title;
    eventCard[QStringLiteral("startTime")] = ev.startTime.toString(QStringLiteral("HH:mm"));
    eventCard[QStringLiteral("endTime")] = ev.endTime.toString(QStringLiteral("HH:mm"));
    eventCard[QStringLiteral("dateFormatted")] = QLocale(QLocale::Spanish, QLocale::Spain).toString(ev.startTime.date(), QStringLiteral("dddd, d 'de' MMMM"));
    eventCard[QStringLiteral("dayNumber")] = ev.startTime.toString(QStringLiteral("d"));
    eventCard[QStringLiteral("monthShort")] = QLocale(QLocale::Spanish, QLocale::Spain).toString(ev.startTime.date(), QStringLiteral("MMM")).toUpper();
    eventCard[QStringLiteral("description")] = ev.description;
    eventCard[QStringLiteral("calendarName")] = QStringLiteral("Google Calendar (César Conte)");
    m_pendingCardType = QStringLiteral("event");
    m_pendingCardData = eventCard;

    KNotification::event(QStringLiteral("notification"),
                         i18n("Cita agendada"),
                         QStringLiteral("%1 (%2)").arg(ev.title, ev.startTime.toString(QStringLiteral("dd/MM/yyyy HH:mm"))),
                         QStringLiteral("x-office-calendar"));

    openCalendarService(ev);
    return true;
}

void KityApplet::openCalendarService(const ParsedCalendarEvent &ev) const
{
    const QString client = readConfigString(QStringLiteral("defaultCalendarClient"), QStringLiteral("korganizer")).toLower();
    if (client == QStringLiteral("google")) {
        QDesktopServices::openUrl(buildGoogleCalendarTemplateUrl(ev));
    } else if (client == QStringLiteral("proton")) {
        const QString protonApp = QStandardPaths::findExecutable(QStringLiteral("proton-mail"));
        if (!protonApp.isEmpty()) {
            QProcess::startDetached(protonApp, QStringList() << QStringLiteral("proton-inbox://open_calendar"));
        } else {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://calendar.proton.me")));
        }
    } else {
        const QString korganizer = QStandardPaths::findExecutable(QStringLiteral("korganizer"));
        if (!korganizer.isEmpty()) {
            QProcess::startDetached(korganizer, QStringList());
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
    QUrl mailtoUrl(QStringLiteral("mailto:") + to);
    QUrlQuery q;
    if (!subject.isEmpty()) q.addQueryItem(QStringLiteral("subject"), subject);
    if (!body.isEmpty()) q.addQueryItem(QStringLiteral("body"), body);
    mailtoUrl.setQuery(q);

    const QString client = service.isEmpty() ? readConfigString(QStringLiteral("defaultEmailClient"), QStringLiteral("proton")) : service;
    if (client == QStringLiteral("gmail")) {
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

    // Official Proton Mail desktop app takes precedence with pre-filled compose window
    const QString protonApp = QStandardPaths::findExecutable(QStringLiteral("proton-mail"));
    if (!protonApp.isEmpty()) {
        QProcess::startDetached(protonApp, QStringList() << mailtoUrl.toString());
        return;
    }

    // Default mailto fallback
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
    mgr.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"));
    req.setRawHeader("Accept-Language", "es-ES,es;q=0.9,en;q=0.8");

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

QByteArray KityApplet::httpPostFormSync(const QUrl &url, const QByteArray &formData, int timeoutMs) const
{
    QNetworkAccessManager mgr;
    mgr.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"));
    req.setRawHeader("Accept-Language", "es-ES,es;q=0.9,en;q=0.8");
    req.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");

    QNetworkReply *reply = mgr.post(req, formData);
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

QVariantMap KityApplet::queryActiveMedia() const
{
    QProcess p;
    p.start(QStringLiteral("playerctl"), {QStringLiteral("metadata"), QStringLiteral("--format"), QStringLiteral("{{ playerName }}|||{{ status }}|||{{ artist }}|||{{ title }}|||{{ album }}|||{{ mpris:artUrl }}")});
    if (!p.waitForFinished(1000)) {
        return QVariantMap();
    }
    const QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    if (out.isEmpty()) {
        return QVariantMap();
    }

    const QStringList parts = out.split(QStringLiteral("|||"));
    if (parts.size() < 4) {
        return QVariantMap();
    }

    QVariantMap res;
    res[QStringLiteral("player")] = parts.value(0).trimmed();
    res[QStringLiteral("status")] = parts.value(1).trimmed();
    res[QStringLiteral("artist")] = parts.value(2).trimmed();
    res[QStringLiteral("title")] = parts.value(3).trimmed();
    res[QStringLiteral("album")] = parts.value(4).trimmed();
    res[QStringLiteral("artUrl")] = parts.value(5).trimmed();
    return res;
}

void KityApplet::controlMedia(const QString &action)
{
    const QString act = action.trimmed().toLower();
    if (act == QStringLiteral("play-pause")) {
        QProcess::execute(QStringLiteral("playerctl"), {QStringLiteral("play-pause")});
    } else if (act == QStringLiteral("next")) {
        QProcess::execute(QStringLiteral("playerctl"), {QStringLiteral("next")});
    } else if (act == QStringLiteral("previous")) {
        QProcess::execute(QStringLiteral("playerctl"), {QStringLiteral("previous")});
    } else if (act == QStringLiteral("stop")) {
        QProcess::execute(QStringLiteral("playerctl"), {QStringLiteral("stop")});
    }
}

void KityApplet::openKOrganizer() const
{
    const QString korganizer = QStandardPaths::findExecutable(QStringLiteral("korganizer"));
    if (!korganizer.isEmpty()) {
        QProcess::startDetached(korganizer, QStringList());
    }
}

QString KityApplet::mobilePairingUrl() const
{
    return m_mobileServer ? m_mobileServer->pairingUrl() : QString();
}

bool KityApplet::shareMobilePairingWithKdeConnect(const QString &deviceId) const
{
    if (!m_mobileServer) {
        return false;
    }
    QString targetDev = deviceId;
    if (targetDev.isEmpty()) {
        const auto devices = getKdeConnectDevices();
        if (!devices.isEmpty()) {
            targetDev = devices.first().id;
        }
    }

    bool ok = false;
    if (!targetDev.isEmpty()) {
        ok = m_mobileServer->sendPairingUrlViaKdeConnect(targetDev);
    }

    if (ok) {
        KNotification::event(QStringLiteral("notification"),
                             i18n("Kity Remote"),
                             i18n("Enlace de emparejamiento enviado a tu teléfono por KDE Connect"));
    } else {
        copyToClipboard(mobilePairingUrl());
        KNotification::event(QStringLiteral("notification"),
                             i18n("Kity Remote"),
                             i18n("Enlace de emparejamiento copiado al portapapeles"));
    }
    return ok;
}

#include "kityapplet.moc"
