/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "kityapplet.h"
#include "mobile/mobileserver.h"
#include "system/windowmanager.h"
#include "system/applicationmanager.h"
#include "system/mediamanager.h"
#include "system/timermanager.h"
#include "system/weathermanager.h"
#include "system/clipboardmanager.h"
#include "system/terminalmanager.h"
#include "system/calendarmanager.h"
#include "system/emailmanager.h"
#include "system/notificationmanager.h"
#include "system/kdeconnectmanager.h"
#include "system/storagemanager.h"
#include "system/configmanager.h"
#include "ai/intentparser.h"
#include "ai/knowledgemanager.h"
#include "ai/visionengine.h"
#include "ai/visualintelligencecontroller.h"
#include "ai/writingtoolsmanager.h"
#include "ai/contextmanager.h"
#include "ai/llmengine.h"
#include "voice/voiceengine.h"
#include "debug.h"

#include <KLocalizedString>
#include <KNotification>
#include <KPluginFactory>

#include <QQmlEngine>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

K_PLUGIN_CLASS_WITH_JSON(KityApplet, "metadata.json")

KityApplet::KityApplet(QObject *parent, const KPluginMetaData &data, const QVariantList &args)
    : Plasma::Applet(parent, data, args)
    , m_nam(new QNetworkAccessManager(this))
    , m_reminderTimer(new QTimer(this))
    , m_readinessTimer(new QTimer(this))
{
    static bool s_qmlTypesRegistered = false;
    if (!s_qmlTypesRegistered) {
        qmlRegisterType<KDictationClient>("org.cesar.kity", 1, 0, "KDictationClient");
        s_qmlTypesRegistered = true;
    }
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
    Kity::StorageManager::instance().loadContacts();
    Kity::TimerManager::instance().loadReminders();
    Kity::KnowledgeManager::instance().ensureKnowledgeBaseReady();

    // Check reminders periodically
    connect(m_reminderTimer, &QTimer::timeout, this, []() {
        Kity::TimerManager::instance().checkReminders();
    });
    m_reminderTimer->start(30000);

    connect(&Kity::TimerManager::instance(), &Kity::TimerManager::timerFinished, this, [this](int, const QString &label) {
        speakText(i18n("El temporizador de %1 ha finalizado.", label));
    });
    connect(&Kity::TimerManager::instance(), &Kity::TimerManager::reminderTriggered, this, [this](const Kity::ScheduledReminder &rem) {
        speakText(i18n("Recordatorio: %1", rem.text));
    });

    // VoiceEngine event bindings
    connect(&Kity::VoiceEngine::instance(), &Kity::VoiceEngine::transcriptionReady, this, [this](const QString &text, bool fromVoice) {
        sendMessage(text, fromVoice);
    });
    connect(&Kity::VoiceEngine::instance(), &Kity::VoiceEngine::transcriptionFailed, this, [this](const QString &err) {
        setState(QStringLiteral("error"), err);
    });
    connect(&Kity::VoiceEngine::instance(), &Kity::VoiceEngine::audioLevelChanged, this, [this](qreal lvl) {
        m_audioLevel = lvl;
        Q_EMIT audioLevelChanged();
    });
    connect(&Kity::VoiceEngine::instance(), &Kity::VoiceEngine::wakeWordTriggered, this, [this]() {
        Q_EMIT wakeWordTriggered();
        openUI();
        startRecording();
    });
    connect(&Kity::VoiceEngine::instance(), &Kity::VoiceEngine::recordingStateChanged, this, [this](bool rec) {
        if (rec) {
            setState(QStringLiteral("listening"), i18n("Escuchando… Habla ahora, Kity detectará cuando termines."));
        } else {
            setState(QStringLiteral("idle"));
        }
    });
    connect(&Kity::VoiceEngine::instance(), &Kity::VoiceEngine::speakingStateChanged, this, [this](bool spk) {
        if (spk) {
            setState(QStringLiteral("speaking"));
        } else if (m_state == QLatin1String("speaking")) {
            setState(QStringLiteral("idle"));
        }
    });
    connect(&Kity::VoiceEngine::instance(), &Kity::VoiceEngine::dictationInjected, this, [this](const QString &text) {
        qInfo() << "Kity: Dictado inyectado remotamente en el PC:" << text;
        if (m_mobileServer) {
            m_mobileServer->broadcastState(QStringLiteral("idle"), i18n("✍️ Dictado insertado en el cursor"));
        }
    });
    connect(&Kity::VoiceEngine::instance(), &Kity::VoiceEngine::kdictationAvailabilityChanged, this, &KityApplet::kdictationAvailableChanged);
    connect(&Kity::VoiceEngine::instance(), &Kity::VoiceEngine::whisperModelStatusChanged, this, [this](bool loaded) {
        m_whisperAvailable = loaded;
        Q_EMIT readinessChanged();
    });

    // LlmEngine event bindings
    connect(&Kity::LlmEngine::instance(), &Kity::LlmEngine::stateChanged, this, [this](const QString &st, const QString &msg) {
        setState(st, msg);
    });
    connect(&Kity::LlmEngine::instance(), &Kity::LlmEngine::streamingTextChanged, this, [this](const QString &text) {
        m_streamingText = text;
        Q_EMIT streamingTextChanged();
    });
    connect(&Kity::LlmEngine::instance(), &Kity::LlmEngine::streamingChunkReceived, this, [](const QString &chunk) {
        Kity::VoiceEngine::instance().processStreamingTtsChunk(chunk);
    });
    connect(&Kity::LlmEngine::instance(), &Kity::LlmEngine::responseFinished, this, [this](const Kity::LlmResponse &resp) {
        m_streamingText.clear();
        Q_EMIT streamingTextChanged();
        if (!resp.cardType.isEmpty() && !resp.cardData.isEmpty()) {
            m_pendingCardType = resp.cardType;
            m_pendingCardData = resp.cardData;
        }
        Kity::VoiceEngine::instance().flushStreamingTtsChunk();
        appendAssistantMessage(resp.text, false);
    });
    connect(&Kity::LlmEngine::instance(), &Kity::LlmEngine::errorOccurred, this, [this](const QString &err) {
        m_streamingText.clear();
        Q_EMIT streamingTextChanged();
        setState(QStringLiteral("error"), err);
    });
    connect(&Kity::LlmEngine::instance(), &Kity::LlmEngine::availableModelsChanged, this, [this](const QStringList &models) {
        m_availableModels = models;
        Q_EMIT availableModelsChanged();
    });
    connect(&Kity::LlmEngine::instance(), &Kity::LlmEngine::ollamaReachabilityChanged, this, [this](bool reachable) {
        m_ollamaReachable = reachable;
        Q_EMIT readinessChanged();
    });

    // Check service health periodically
    connect(m_readinessTimer, &QTimer::timeout, this, &KityApplet::checkReadiness);
    m_readinessTimer->start(60000);

    checkReadiness();
    refreshModels();

    m_wakeWordEnabled = Kity::ConfigManager::instance().wakeWordEnabled();
    const bool centralizedEnabled = useCentralizedVoice();
    const bool kdictationReady = Kity::VoiceEngine::instance().isKDictationAvailable();

    if (!centralizedEnabled || !kdictationReady || m_wakeWordEnabled) {
        const QString whisperModel = Kity::ConfigManager::instance().whisperModelPath();
        if (!whisperModel.isEmpty()) {
            Kity::VoiceEngine::instance().preloadWhisperModel(whisperModel);
        }
    } else {
        qInfo() << "Kity: Servicio KDictation activo en D-Bus. Se omite precarga de Whisper en RAM de plasmashell.";
    }

    if (m_wakeWordEnabled) {
        startWakeWordListener();
    }

    // Register Kity on session D-Bus for Spotlight / KRunner / App integration
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.registerService(QStringLiteral("org.cesar.kity"));
    bus.registerObject(QStringLiteral("/Kity"),
                       this,
                       QDBusConnection::ExportAllInvokables | QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSignals);

    // Global shortcut Meta+K
    if (globalShortcut().isEmpty()) {
        setGlobalShortcut(QKeySequence(QStringLiteral("Meta+K")));
    }
    connect(this, &Plasma::Applet::activated, this, &KityApplet::toggleUI);

    // Mobile Remote Server (Apple Intelligence PWA)
    m_mobileServer = new KityMobileServer(1740, this);
    connect(m_mobileServer, &KityMobileServer::voiceCommandReceived, this, [this](const QString &text, bool fromVoice) {
        sendMessage(text, fromVoice);
    });
    connect(m_mobileServer, &KityMobileServer::voiceAudioReceived, this, [](const QString &wavPath) {
        Kity::VoiceEngine::instance().transcribeWavFile(wavPath);
    });
    connect(m_mobileServer, &KityMobileServer::dictationAudioReceived, this, [this](const QString &wavPath) {
        QFile file(wavPath);
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray data = file.readAll();
            file.close();
            QFile::remove(wavPath);
            const QByteArray pcm = data.size() > 44 ? data.mid(44) : data;
            if (!pcm.isEmpty()) {
                if (m_mobileServer) m_mobileServer->broadcastState(QStringLiteral("thinking"), i18n("✍️ Dictando en PC (KDictation)…"));
                Kity::VoiceEngine::instance().injectDictationPcm(pcm);
            }
        }
    });
    connect(m_mobileServer, &KityMobileServer::dictationTextReceived, this, [this](const QString &text) {
        if (!text.trimmed().isEmpty()) {
            if (m_mobileServer) m_mobileServer->broadcastState(QStringLiteral("thinking"), i18n("✍️ Insertando dictado en PC…"));
            Kity::VoiceEngine::instance().injectDictationText(text);
        }
    });
    connect(this, &KityApplet::stateChanged, this, [this]() {
        if (m_mobileServer) m_mobileServer->broadcastState(state(), m_statusMessage);
    });
    connect(this, &KityApplet::audioLevelChanged, this, [this]() {
        if (m_mobileServer) m_mobileServer->broadcastAudioLevel(m_audioLevel);
    });
    m_mobileServer->start();

    // Configure action menu
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
    setWakeWordEnabled(Kity::ConfigManager::instance().wakeWordEnabled());
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
bool KityApplet::isSpeaking() const { return m_state == QLatin1String("speaking") || Kity::VoiceEngine::instance().isSpeaking(); }
qreal KityApplet::audioLevel() const { return m_audioLevel; }
bool KityApplet::wakeWordEnabled() const { return m_wakeWordEnabled; }

void KityApplet::setWakeWordEnabled(bool enabled)
{
    if (m_wakeWordEnabled != enabled) {
        m_wakeWordEnabled = enabled;
        Kity::ConfigManager::instance().setWakeWordEnabled(enabled);
        Q_EMIT wakeWordEnabledChanged();
        if (m_wakeWordEnabled) {
            startWakeWordListener();
        } else {
            stopWakeWordListener();
        }
    }
}

QString KityApplet::speechOutputMode() const { return Kity::ConfigManager::instance().speechOutputMode(); }
void KityApplet::setSpeechOutputMode(const QString &mode)
{
    Kity::ConfigManager::instance().setSpeechOutputMode(mode);
    Q_EMIT speechOutputModeChanged();
}

QString KityApplet::panelIconStyle() const { return Kity::ConfigManager::instance().panelIconStyle(); }
void KityApplet::setPanelIconStyle(const QString &style)
{
    Kity::ConfigManager::instance().setPanelIconStyle(style);
    Q_EMIT panelIconStyleChanged();
}

bool KityApplet::vadEnabled() const { return Kity::ConfigManager::instance().vadEnabled(); }
void KityApplet::setVadEnabled(bool enabled)
{
    Kity::ConfigManager::instance().setVadEnabled(enabled);
    Q_EMIT vadEnabledChanged();
}

bool KityApplet::earconsEnabled() const { return Kity::ConfigManager::instance().earconsEnabled(); }
void KityApplet::setEarconsEnabled(bool enabled)
{
    Kity::ConfigManager::instance().setEarconsEnabled(enabled);
    Q_EMIT earconsEnabledChanged();
}

bool KityApplet::useCentralizedVoice() const { return Kity::ConfigManager::instance().useCentralizedVoice(); }
void KityApplet::setUseCentralizedVoice(bool enabled)
{
    Kity::ConfigManager::instance().setUseCentralizedVoice(enabled);
    Q_EMIT useCentralizedVoiceChanged();
}

bool KityApplet::isKDictationAvailable() const
{
    return Kity::VoiceEngine::instance().isKDictationAvailable();
}

bool KityApplet::checkKDictationAvailable()
{
    Kity::VoiceEngine::instance().checkKDictationAvailability();
    const bool available = Kity::VoiceEngine::instance().isKDictationAvailable();
    Q_EMIT kdictationAvailableChanged();
    return available;
}

QString KityApplet::ollamaModel() const { return Kity::ConfigManager::instance().ollamaModel(); }
void KityApplet::setOllamaModel(const QString &model)
{
    Kity::ConfigManager::instance().setOllamaModel(model);
    Q_EMIT ollamaModelChanged();
}

void KityApplet::setState(const QString &newState, const QString &newStatus)
{
    qWarning() << "Kity state transition:" << newState << "Status:" << newStatus;
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

    if (stateChangedFlag) Q_EMIT stateChanged();
    if (oldStatus != m_statusMessage) Q_EMIT statusMessageChanged();

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
// Health & Readiness
// =========================================================================

void KityApplet::checkReadiness()
{
    Kity::VoiceEngine::instance().checkKDictationAvailability();

    const bool centralizedReady = Kity::VoiceEngine::instance().isKDictationAvailable();
    const bool hasWhisperModel = centralizedReady || Kity::VoiceEngine::instance().isWhisperModelLoaded() || !Kity::ConfigManager::instance().whisperModelPath().isEmpty();
    const bool newWhisperAvailable = hasWhisperModel;

    const bool hasPiperCli = !QStandardPaths::findExecutable(QStringLiteral("piper")).isEmpty();
    const bool hasPiperVoice = !Kity::ConfigManager::instance().piperVoicePath().isEmpty();
    const bool hasKokoroCli = !QStandardPaths::findExecutable(QStringLiteral("kity-kokoro-tts")).isEmpty();
    const bool hasEdgeCli = !QStandardPaths::findExecutable(QStringLiteral("kity-edge-tts")).isEmpty();
    const bool hasPwPlay = !QStandardPaths::findExecutable(QStringLiteral("pw-play")).isEmpty();
    const bool newPiperAvailable = ((hasPiperCli && hasPiperVoice) || hasKokoroCli || hasEdgeCli) && hasPwPlay;

    const QUrl tagsUrl(Kity::ConfigManager::instance().ollamaUrl() + QStringLiteral("/api/tags"));
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
                if (!mName.isEmpty()) models.append(mName);
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

        if (readinessChangedFlag) Q_EMIT readinessChanged();
        if (modelsChanged) Q_EMIT availableModelsChanged();
    });
}

void KityApplet::refreshModels()
{
    checkReadiness();
}

// =========================================================================
// Conversation & LLM Interaction
// =========================================================================

void KityApplet::appendAssistantMessage(const QString &text, bool speakIfAppropriate)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return;

    QVariantMap assistantMsg;
    assistantMsg[QStringLiteral("role")] = QStringLiteral("assistant");
    assistantMsg[QStringLiteral("content")] = trimmed;
    assistantMsg[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);

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
        const QString speechMode = speechOutputMode();
        if (speechMode == QLatin1String("always") || (speechMode == QLatin1String("voice_only") && m_lastQueryWasVoice)) {
            speakText(trimmed);
            return;
        }
    }

    if (!Kity::VoiceEngine::instance().isSpeaking()) {
        setState(QStringLiteral("idle"), QString());
    }
}

void KityApplet::sendMessage(const QString &text, bool fromVoice)
{
    QString trimmed = text.trimmed();
    if (fromVoice) {
        trimmed = Kity::VoiceEngine::removeFillerWords(trimmed, Kity::ConfigManager::instance().whisperLanguage());
    }
    if (trimmed.isEmpty()) return;

    stopSpeaking();
    m_lastQueryWasVoice = fromVoice;
    Kity::VoiceEngine::instance().setLastQueryWasVoice(fromVoice);

    QVariantMap userMsg;
    userMsg[QStringLiteral("role")] = QStringLiteral("user");
    userMsg[QStringLiteral("content")] = trimmed;
    userMsg[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    userMsg[QStringLiteral("fromVoice")] = fromVoice;
    m_messages.append(userMsg);
    Q_EMIT messagesChanged();
    saveConversationHistory();

    const QString mode = Kity::ConfigManager::instance().siriMode();
    if (mode == QLatin1String("off")) {
        appendAssistantMessage(i18n("Kity está desactivado en las Preferencias (Assistant Experience: Off)."), false);
        return;
    }

    if (detectAndExecuteSystemAction(trimmed)) {
        return;
    }

    // Gather contextual knowledge (weather, web grounding, system info, RAG, clipboard, notifications)
    const auto ctxRes = Kity::ContextManager::instance().gatherContext(trimmed);
    if (ctxRes.handledEarly) {
        appendAssistantMessage(ctxRes.earlyAssistantResponse, false);
        return;
    }
    if (!ctxRes.pendingCardType.isEmpty()) {
        m_pendingCardType = ctxRes.pendingCardType;
        m_pendingCardData = ctxRes.pendingCardData;
    }

    QString effectivePrompt = ctxRes.effectivePrompt;
    QString context = ctxRes.context;
    QString base64Image;

    // Vision action: screen capture inspection if requested
    static const QRegularExpression screenRegex(
        QStringLiteral("(?:(?:mi|la)\\s+pantalla|en\\s+(?:mi\\s+|la\\s+)?pantalla|captura(?:r)?\\s+(?:de\\s+pantalla|la\\s+pantalla)|analiza(?:r)?\\s+(?:mi\\s+|la\\s+)?pantalla|qu[eé]\\s+hay\\s+en\\s+(?:pantalla|el\\s+escritorio)|qu[eé]\\s+ves\\s+en\\s+pantalla|qu[eé]\\s+aplicaci[oó]n\\s+se\\s+muestra)"),
        QRegularExpression::CaseInsensitiveOption);
    if (!trimmed.contains(QLatin1String("\"\"\"")) && trimmed.contains(screenRegex)) {
        setState(QStringLiteral("thinking"), i18n("Analizando las ventanas y el contenido de tu pantalla…"));
        QCoreApplication::processEvents();

        const QString kwinWindows = Kity::ContextManager::instance().collectKWinWindowsInfo();
        const QString tmpShot = QDir::tempPath() + QStringLiteral("/kity_vision_%1.png").arg(QDateTime::currentMSecsSinceEpoch());
        const QString savedShot = Kity::VisionEngine::instance().captureScreenToPng(tmpShot, QStringLiteral("fullscreen"));
        const QString ocrText = Kity::VisionEngine::instance().extractScreenOcrText(savedShot);
        base64Image = Kity::VisionEngine::instance().encodeImageToBase64(savedShot);
        QFile::remove(savedShot);

        effectivePrompt = QStringLiteral(
            "Eres el asistente multimodal de Kity. El usuario te pregunta: \"%1\"\n\n"
            "A continuación dispones de la información de pantalla en KDE Plasma Wayland:\n\n"
            "[Ventanas abiertas en KWin Wayland]:\n%2\n\n"
            "[Texto visible detectado mediante Live Text OCR]:\n\"\"\"\n%3\n\"\"\"\n\n"
            "Responde en español de España de forma concisa y directa.")
            .arg(trimmed, kwinWindows, ocrText);
    }

    // Route between Gemini Cloud and Ollama Local
    const QString aiBackend = Kity::ConfigManager::instance().aiBackend();
    const QString geminiApiKey = Kity::ConfigManager::instance().geminiApiKey();
    bool useGemini = false;

    if (aiBackend == QLatin1String("gemini")) {
        useGemini = !geminiApiKey.isEmpty();
    } else if (aiBackend == QLatin1String("hybrid")) {
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

    Kity::LlmEngine::instance().query(effectivePrompt, context, base64Image, m_messages, useGemini);
}

void KityApplet::stopGeneration()
{
    Kity::LlmEngine::instance().stop();
    Kity::VoiceEngine::instance().stopRecording();
    stopSpeaking();
    setState(QStringLiteral("idle"), i18n("Generación cancelada."));
}

bool KityApplet::detectAndExecuteSystemAction(const QString &input)
{
    const auto res = Kity::IntentParser::instance().execute(input);
    if (res.handled) {
        if (!res.cardType.isEmpty()) {
            m_pendingCardType = res.cardType;
            m_pendingCardData = res.cardData;
        }
        if (!res.feedback.isEmpty()) {
            appendAssistantMessage(res.feedback);
        }
        return true;
    }
    return false;
}

void KityApplet::openUI()
{
    Q_EMIT openRequested();
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.interface() || !bus.interface()->isServiceRegistered(QStringLiteral("org.cesar.kity_app"))) {
        const QString kityBin = QStandardPaths::findExecutable(QStringLiteral("kity"));
        if (!kityBin.isEmpty()) QProcess::startDetached(kityBin, QStringList());
    }
}

void KityApplet::toggleUI()
{
    Q_EMIT toggleRequested();
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.interface() || !bus.interface()->isServiceRegistered(QStringLiteral("org.cesar.kity_app"))) {
        const QString kityBin = QStandardPaths::findExecutable(QStringLiteral("kity"));
        if (!kityBin.isEmpty()) QProcess::startDetached(kityBin, QStringList() << QStringLiteral("-t"));
    }
}

void KityApplet::openSettings()
{
    QProcess::startDetached(QStringLiteral("systemsettings"), QStringList() << QStringLiteral("kcm_kity"));
}

void KityApplet::reloadConfig()
{
    Kity::ConfigManager::instance().reload();
    Kity::VoiceEngine::instance().reloadConfig();
    setWakeWordEnabled(Kity::ConfigManager::instance().wakeWordEnabled());
    Q_EMIT panelIconStyleChanged();
    Q_EMIT speechOutputModeChanged();
    Q_EMIT vadEnabledChanged();
    Q_EMIT earconsEnabledChanged();
    Q_EMIT useCentralizedVoiceChanged();
    Q_EMIT ollamaModelChanged();
    checkKDictationAvailable();
    checkReadiness();
    refreshModels();
}

void KityApplet::clearHistory()
{
    m_messages.clear();
    Q_EMIT messagesChanged();
    QFile::remove(Kity::StorageManager::instance().historyFilePath());
    if (!Kity::VoiceEngine::instance().isSpeaking()) {
        setState(QStringLiteral("idle"), QString());
    }
}

// =========================================================================
// Apple Intelligence Writing Tools
// =========================================================================

QString KityApplet::readWaylandSelectionOrClipboard() const
{
    return Kity::ClipboardManager::instance().readWaylandSelectionOrClipboard();
}

QString KityApplet::buildWritingToolPrompt(const QString &action, const QString &sourceText)
{
    return Kity::WritingToolsManager::instance().buildPrompt(action, sourceText);
}

void KityApplet::applyWritingTool(const QString &action, const QString &customSourceText)
{
    const QString source = customSourceText.isEmpty() ? readWaylandSelectionOrClipboard() : customSourceText;
    if (source.trimmed().isEmpty()) {
        appendAssistantMessage(i18n("No hay ningún texto seleccionado ni en el portapapeles para procesar con las Herramientas de Escritura."));
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
    userMsg[QStringLiteral("isWritingTool")] = true;
    userMsg[QStringLiteral("sourceText")] = source;
    userMsg[QStringLiteral("writingToolAction")] = action;
    m_messages.append(userMsg);
    Q_EMIT messagesChanged();
    saveConversationHistory();

    m_lastQueryWasVoice = false;
    const QString fullPrompt = buildWritingToolPrompt(action, source);

    const QString aiBackend = Kity::ConfigManager::instance().aiBackend();
    const QString geminiApiKey = Kity::ConfigManager::instance().geminiApiKey();
    const bool useGemini = (aiBackend == QLatin1String("gemini") || (aiBackend == QLatin1String("hybrid") && !geminiApiKey.isEmpty()));

    Kity::LlmEngine::instance().query(fullPrompt, QString(), QString(), m_messages, useGemini);
}

QString KityApplet::languageDisplayName(const QString &code)
{
    return Kity::WritingToolsManager::instance().languageDisplayName(code);
}

void KityApplet::translateSelection(const QString &targetLanguage)
{
    applyWritingTool(QStringLiteral("translate:") + (targetLanguage.isEmpty() ? QStringLiteral("es") : targetLanguage));
}

QStringList KityApplet::evaluateMathNotes(const QString &text) const
{
    return Kity::WritingToolsManager::instance().evaluateMath(text);
}

// =========================================================================
// Visual Intelligence & Live Text
// =========================================================================

QVariantMap KityApplet::parseLiveTextEntities(const QString &ocrText)
{
    return Kity::VisionEngine::parseLiveTextEntities(ocrText);
}

void KityApplet::runVisualIntelligence(const QString &captureMode, const QString &action, const QString &customQuestion)
{
    const QString mode = captureMode.isEmpty() ? QStringLiteral("region") : captureMode;
    const bool isDirectFile = mode.startsWith(QStringLiteral("file:"));

    if (isDirectFile) {
        setState(QStringLiteral("thinking"), i18n("Analizando archivo de imagen «%1»…", QFileInfo(mode.mid(5)).fileName()));
    } else if (mode == QStringLiteral("region")) {
        setState(QStringLiteral("thinking"), i18n("Selecciona una región en pantalla con el ratón (Spectacle Wayland)…"));
    } else if (mode == QStringLiteral("activewindow")) {
        setState(QStringLiteral("thinking"), i18n("Capturando la ventana activa (Spectacle Wayland)…"));
    } else {
        setState(QStringLiteral("thinking"), i18n("Capturando la pantalla completa (Spectacle Wayland)…"));
    }
    QCoreApplication::processEvents();

    const auto res = Kity::VisualIntelligenceController::instance().process(captureMode, action, customQuestion);
    if (!res.success) {
        setState(QStringLiteral("idle"), res.errorMessage);
        return;
    }

    if (res.handledEarly) {
        appendAssistantMessage(res.assistantFeedback, false);
        setState(QStringLiteral("idle"), QString());
        openUI();
        return;
    }

    QVariantMap userMsg;
    userMsg[QStringLiteral("role")] = QStringLiteral("user");
    userMsg[QStringLiteral("content")] = res.userMessageLabel;
    userMsg[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    m_messages.append(userMsg);
    Q_EMIT messagesChanged();
    saveConversationHistory();

    m_lastQueryWasVoice = false;
    const QString aiBackend = Kity::ConfigManager::instance().aiBackend();
    const QString geminiApiKey = Kity::ConfigManager::instance().geminiApiKey();
    const bool useGemini = (aiBackend == QLatin1String("gemini") || (aiBackend == QLatin1String("hybrid") && !geminiApiKey.isEmpty()));

    Kity::LlmEngine::instance().query(res.fullPrompt, QString(), res.base64Image, m_messages, useGemini);
}

// =========================================================================
// Subsystem Delegations (High Cohesion, Low Coupling)
// =========================================================================

void KityApplet::startRecording() { Kity::VoiceEngine::instance().startRecording(); }
void KityApplet::stopRecording() { Kity::VoiceEngine::instance().stopRecording(); }
void KityApplet::toggleRecording() { Kity::VoiceEngine::instance().toggleRecording(); }
void KityApplet::speakText(const QString &text) { Kity::VoiceEngine::instance().speakText(text); }
void KityApplet::stopSpeaking() { Kity::VoiceEngine::instance().stopSpeaking(); }
void KityApplet::startWakeWordListener() { Kity::VoiceEngine::instance().startWakeWordListener(); }
void KityApplet::stopWakeWordListener() { Kity::VoiceEngine::instance().stopWakeWordListener(); }

bool KityApplet::launchApplication(const QString &appQuery, QString *launchedNameOut)
{
    return Kity::ApplicationManager::instance().launchApp(appQuery, launchedNameOut);
}

bool KityApplet::closeApplication(const QString &appQuery, QString *closedNameOut)
{
    return Kity::ApplicationManager::instance().closeApp(appQuery, closedNameOut);
}

QStringList KityApplet::extractTerminalCommands(const QString &markdown) const
{
    return Kity::TerminalManager::instance().extractTerminalCommands(markdown);
}

void KityApplet::executeCommandInKonsole(const QString &command)
{
    Kity::TerminalManager::instance().executeInKonsole(command);
}

void KityApplet::copyToClipboard(const QString &text) const
{
    Kity::ClipboardManager::instance().copyToClipboard(text);
}

void KityApplet::replaceText(const QString &text) const
{
    copyToClipboard(text);
    KNotification::event(QStringLiteral("notification"),
                         i18n("Herramientas de Escritura"),
                         i18n("Texto copiado al portapapeles. Listo para pegar (Ctrl+V) en tu aplicación."),
                         QStringLiteral("org.cesar.kity"));
}

QString KityApplet::renderMarkdown(const QString &markdown) const
{
    return Kity::WritingToolsManager::instance().renderMarkdown(markdown);
}

void KityApplet::openKnowledgeFolder() const
{
    Kity::KnowledgeManager::instance().openKnowledgeFolder();
}

int KityApplet::knowledgeDocumentCount() const
{
    return Kity::KnowledgeManager::instance().knowledgeDocumentCount();
}

QString KityApplet::collectKWinWindowsInfo() const
{
    return Kity::ContextManager::instance().collectKWinWindowsInfo();
}

void KityApplet::controlMedia(const QString &action)
{
    Kity::MediaManager::instance().executeAction(action);
}

QVariantMap KityApplet::queryActiveMedia() const
{
    return Kity::MediaManager::instance().queryActiveMedia().toMap();
}

QVariantMap KityApplet::fetchWeatherData(const QString &query) const
{
    return Kity::WeatherManager::instance().fetchWeatherData(query).toMap();
}

void KityApplet::openKOrganizer() const
{
    const QString korganizer = QStandardPaths::findExecutable(QStringLiteral("korganizer"));
    if (!korganizer.isEmpty()) {
        QProcess::startDetached(korganizer, QStringList());
    }
}

bool KityApplet::createCalendarEvent(const QString &title, const QString &datePrompt)
{
    const QString fullPrompt = datePrompt.isEmpty() ? title : QStringLiteral("%1 %2").arg(title, datePrompt);
    const Kity::CalendarEvent ev = Kity::CalendarManager::instance().parsePrompt(fullPrompt);
    return Kity::CalendarManager::instance().createEvent(ev);
}

void KityApplet::composeEmail(const QString &to, const QString &subject, const QString &body, const QString &service) const
{
    const QString client = service.isEmpty() ? Kity::ConfigManager::instance().defaultEmailClient() : service;
    Kity::EmailManager::instance().composeEmail(to, subject, body, client);
}

void KityApplet::sendPendingEmail()
{
    QString err;
    if (Kity::EmailManager::instance().sendPendingDraft(&err)) {
        appendAssistantMessage(i18n("✓ Correo enviado con éxito mediante Proton Mail."));
    } else {
        appendAssistantMessage(err.isEmpty() ? i18n("No se pudo enviar el correo.") : err);
    }
}

void KityApplet::openPendingEmailInProton() const
{
    Kity::EmailManager::instance().openInProtonDesktop(Kity::EmailManager::instance().currentDraft());
}

void KityApplet::discardPendingEmail()
{
    if (Kity::EmailManager::instance().discardPendingDraft()) {
        appendAssistantMessage(i18n("✕ Borrador de correo descartado y eliminado."));
    }
}

QString KityApplet::mobilePairingUrl() const
{
    return m_mobileServer ? m_mobileServer->pairingUrl() : QString();
}

bool KityApplet::shareMobilePairingWithKdeConnect(const QString &deviceId) const
{
    if (!m_mobileServer) return false;

    QString targetDev = deviceId;
    if (targetDev.isEmpty()) {
        const auto devices = Kity::KdeConnectManager::instance().getDevices();
        if (!devices.isEmpty()) targetDev = devices.first().id;
    }

    bool ok = false;
    if (!targetDev.isEmpty()) {
        const QString runCmdConfigPath = QStringLiteral("%1/.config/kdeconnect/%2/kdeconnect_runcommand/config")
                                             .arg(QDir::homePath(), targetDev);
        QSettings s(runCmdConfigPath, QSettings::IniFormat);
        QJsonObject cmds = QJsonDocument::fromJson(s.value(QStringLiteral("commands")).toByteArray()).object();
        QJsonObject kityEntry;
        kityEntry[QStringLiteral("name")] = QString::fromUtf8("🔮 Abrir Kity Remote");
        kityEntry[QStringLiteral("command")] =
            QStringLiteral("/usr/bin/qdbus6 org.cesar.kity /Kity org.cesar.kity.shareMobilePairingWithKdeConnect \"\"");
        cmds[QStringLiteral("kity_remote_open")] = kityEntry;
        s.setValue(QStringLiteral("commands"), QJsonDocument(cmds).toJson(QJsonDocument::Compact));

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

void KityApplet::loadConversationHistory()
{
    m_messages = Kity::StorageManager::instance().loadHistory();
    Q_EMIT messagesChanged();
}

void KityApplet::saveConversationHistory()
{
    Kity::StorageManager::instance().saveHistory(m_messages);
}

#include "kityapplet.moc"
