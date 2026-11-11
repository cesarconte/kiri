/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "kityclient.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusArgument>
#include <QDBusVariant>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTextDocument>
#include <QUuid>
#include <KSharedConfig>
#include <KConfigGroup>

// =========================================================================
// KityConfigHelper implementation
// =========================================================================

KityConfigHelper::KityConfigHelper(KityClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
    if (m_client) {
        connect(m_client, &KityClient::wakeWordEnabledChanged, this, &KityConfigHelper::wakeWordEnabledChanged);
        connect(m_client, &KityClient::speechOutputModeChanged, this, &KityConfigHelper::speechOutputModeChanged);
        connect(m_client, &KityClient::vadEnabledChanged, this, &KityConfigHelper::vadEnabledChanged);
        connect(m_client, &KityClient::earconsEnabledChanged, this, &KityConfigHelper::earconsEnabledChanged);
    }
}

bool KityConfigHelper::wakeWordEnabled() const
{
    return m_client ? m_client->wakeWordEnabled() : true;
}

void KityConfigHelper::setWakeWordEnabled(bool enabled)
{
    if (m_client) {
        m_client->setWakeWordEnabled(enabled);
    }
}

QString KityConfigHelper::speechOutputMode() const
{
    return m_client ? m_client->speechOutputMode() : QStringLiteral("always");
}

void KityConfigHelper::setSpeechOutputMode(const QString &mode)
{
    if (m_client) {
        m_client->setSpeechOutputMode(mode);
    }
}

bool KityConfigHelper::vadEnabled() const
{
    return m_client ? m_client->vadEnabled() : true;
}

void KityConfigHelper::setVadEnabled(bool enabled)
{
    if (m_client) {
        m_client->setVadEnabled(enabled);
    }
}

bool KityConfigHelper::earconsEnabled() const
{
    return m_client ? m_client->earconsEnabled() : true;
}

void KityConfigHelper::setEarconsEnabled(bool enabled)
{
    if (m_client) {
        m_client->setEarconsEnabled(enabled);
    }
}

void KityConfigHelper::setOllamaUrl(const QString &url)
{
    if (m_ollamaUrl != url) {
        m_ollamaUrl = url;
        Q_EMIT ollamaUrlChanged();
    }
}

void KityConfigHelper::setPin(bool pinned)
{
    if (m_pin != pinned) {
        m_pin = pinned;
        Q_EMIT pinChanged();
    }
}

// =========================================================================
// KityClient implementation
// =========================================================================

KityClient::KityClient(QObject *parent)
    : QObject(parent)
    , m_watcher(new QDBusServiceWatcher(QStringLiteral("org.cesar.kity"),
                                        QDBusConnection::sessionBus(),
                                        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration,
                                        this))
    , m_configHelper(new KityConfigHelper(this, this))
{
    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered, this, &KityClient::onServiceRegistered);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, &KityClient::onServiceUnregistered);

    setupDBus();
    loadLocalHistory();

    KSharedConfig::Ptr kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    KConfigGroup cfg = kityrc->group(QStringLiteral("General"));
    m_ollamaModel = cfg.readEntry(QStringLiteral("ollamaModel"), QStringLiteral("llama3.2:3b"));
    m_sidebarViewMode = cfg.readEntry(QStringLiteral("sidebarViewMode"), QStringLiteral("grid"));
    m_siriMode = cfg.readEntry(QStringLiteral("siriMode"), QStringLiteral("ai"));
    m_voiceAccent = cfg.readEntry(QStringLiteral("voiceAccent"), QStringLiteral("Spanish (Spain)"));
    m_voiceIndex = cfg.readEntry(QStringLiteral("voiceIndex"), 3);
    m_voicePace = cfg.readEntry(QStringLiteral("voicePace"), 0.55);
    m_voiceExpressivity = cfg.readEntry(QStringLiteral("voiceExpressivity"), 0.65);
    m_historyRetention = cfg.readEntry(QStringLiteral("historyRetention"), QStringLiteral("forever"));
    m_previewLines = cfg.readEntry(QStringLiteral("previewLines"), 4);
    m_openToMode = cfg.readEntry(QStringLiteral("openToMode"), QStringLiteral("past"));
    m_textScale = cfg.readEntry(QStringLiteral("textScale"), 1.0);

    loadConversations();
    connect(this, &KityClient::messagesChanged, this, &KityClient::syncActiveConversationFromMessages);
}

void KityClient::setupDBus()
{
    if (m_iface) {
        delete m_iface;
        m_iface = nullptr;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.interface()->isServiceRegistered(QStringLiteral("org.cesar.kity"))) {
        m_iface = new QDBusInterface(QStringLiteral("org.cesar.kity"),
                                     QStringLiteral("/Kity"),
                                     QStringLiteral("org.cesar.kity"),
                                     bus,
                                     this);
        if (!m_iface->isValid()) {
            delete m_iface;
            m_iface = new QDBusInterface(QStringLiteral("org.cesar.kity"),
                                         QStringLiteral("/Kity"),
                                         QStringLiteral("org.kde.plasmashell.KityApplet"),
                                         bus,
                                         this);
        }

        m_connected = m_iface->isValid();
        Q_EMIT connectionChanged();

        if (m_connected) {
            bus.connect(QStringLiteral("org.cesar.kity"),
                        QStringLiteral("/Kity"),
                        m_iface->interface(),
                        QStringLiteral("stateChanged"),
                        this,
                        SLOT(syncFromDBus()));

            bus.connect(QStringLiteral("org.cesar.kity"),
                        QStringLiteral("/Kity"),
                        m_iface->interface(),
                        QStringLiteral("statusMessageChanged"),
                        this,
                        SLOT(syncFromDBus()));

            bus.connect(QStringLiteral("org.cesar.kity"),
                        QStringLiteral("/Kity"),
                        m_iface->interface(),
                        QStringLiteral("streamingTextChanged"),
                        this,
                        SLOT(syncFromDBus()));

            bus.connect(QStringLiteral("org.cesar.kity"),
                        QStringLiteral("/Kity"),
                        m_iface->interface(),
                        QStringLiteral("messagesChanged"),
                        this,
                        SLOT(syncFromDBus()));

            bus.connect(QStringLiteral("org.cesar.kity"),
                        QStringLiteral("/Kity"),
                        m_iface->interface(),
                        QStringLiteral("audioLevelChanged"),
                        this,
                        SLOT(syncFromDBus()));

            bus.connect(QStringLiteral("org.cesar.kity"),
                        QStringLiteral("/Kity"),
                        m_iface->interface(),
                        QStringLiteral("readinessChanged"),
                        this,
                        SLOT(syncFromDBus()));

            bus.connect(QStringLiteral("org.cesar.kity"),
                        QStringLiteral("/Kity"),
                        m_iface->interface(),
                        QStringLiteral("speechOutputModeChanged"),
                        this,
                        SLOT(syncFromDBus()));

            bus.connect(QStringLiteral("org.cesar.kity"),
                        QStringLiteral("/Kity"),
                        m_iface->interface(),
                        QStringLiteral("vadEnabledChanged"),
                        this,
                        SLOT(syncFromDBus()));

            bus.connect(QStringLiteral("org.cesar.kity"),
                        QStringLiteral("/Kity"),
                        m_iface->interface(),
                        QStringLiteral("earconsEnabledChanged"),
                        this,
                        SLOT(syncFromDBus()));

            bus.connect(QStringLiteral("org.cesar.kity"),
                        QStringLiteral("/Kity"),
                        m_iface->interface(),
                        QStringLiteral("ollamaModelChanged"),
                        this,
                        SLOT(syncFromDBus()));

            syncFromDBus();
        }
    } else {
        m_connected = false;
        Q_EMIT connectionChanged();
    }
}

void KityClient::onServiceRegistered(const QString &service)
{
    Q_UNUSED(service)
    setupDBus();
}

void KityClient::onServiceUnregistered(const QString &service)
{
    Q_UNUSED(service)
    m_connected = false;
    Q_EMIT connectionChanged();
}

static QVariant cleanDbusItem(const QVariant &item)
{
    if (item.canConvert<QDBusVariant>()) {
        return cleanDbusItem(item.value<QDBusVariant>().variant());
    }
    if (item.canConvert<QDBusArgument>()) {
        const QDBusArgument arg = item.value<QDBusArgument>();
        if (arg.currentType() == QDBusArgument::MapType) {
            QVariantMap map;
            arg.beginMap();
            while (!arg.atEnd()) {
                QString key;
                QVariant val;
                arg.beginMapEntry();
                arg >> key >> val;
                arg.endMapEntry();
                map.insert(key, cleanDbusItem(val));
            }
            arg.endMap();
            return map;
        } else if (arg.currentType() == QDBusArgument::ArrayType) {
            QVariantList list;
            arg.beginArray();
            while (!arg.atEnd()) {
                QVariant elem;
                arg >> elem;
                list.append(cleanDbusItem(elem));
            }
            arg.endArray();
            return list;
        }
    }
    return item;
}

void KityClient::syncFromDBus()
{
    if (!m_iface || !m_iface->isValid()) {
        return;
    }

    const QString nextState = m_iface->property("state").toString();
    if (m_state != nextState) {
        m_state = nextState;
        Q_EMIT stateChanged();
    }

    const QString nextStatus = m_iface->property("statusMessage").toString();
    if (m_statusMessage != nextStatus) {
        m_statusMessage = nextStatus;
        Q_EMIT statusMessageChanged();
    }

    const QString nextStream = m_iface->property("streamingText").toString();
    if (m_streamingText != nextStream) {
        m_streamingText = nextStream;
        Q_EMIT streamingTextChanged();
    }

    const qreal nextLevel = m_iface->property("audioLevel").toDouble();
    if (qAbs(m_audioLevel - nextLevel) > 0.005) {
        m_audioLevel = nextLevel;
        Q_EMIT audioLevelChanged();
    }

    const bool nextRec = m_iface->property("isRecording").toBool();
    const bool nextBusy = m_iface->property("isBusy").toBool();
    const bool nextSpeaking = m_iface->property("isSpeaking").toBool();
    if (m_isRecording != nextRec || m_isBusy != nextBusy || m_isSpeaking != nextSpeaking) {
        m_isRecording = nextRec;
        m_isBusy = nextBusy;
        m_isSpeaking = nextSpeaking;
        Q_EMIT stateChanged();
    }

    const bool nextOllama = m_iface->property("ollamaReachable").toBool();
    const bool nextWhisper = m_iface->property("whisperAvailable").toBool();
    const bool nextPiper = m_iface->property("piperAvailable").toBool();
    if (m_ollamaReachable != nextOllama || m_whisperAvailable != nextWhisper || m_piperAvailable != nextPiper) {
        m_ollamaReachable = nextOllama;
        m_whisperAvailable = nextWhisper;
        m_piperAvailable = nextPiper;
        Q_EMIT readinessChanged();
    }

    const QStringList nextModels = m_iface->property("availableModels").toStringList();
    if (m_availableModels != nextModels) {
        m_availableModels = nextModels;
        Q_EMIT availableModelsChanged();
    }

    const bool nextWw = m_iface->property("wakeWordEnabled").toBool();
    if (m_wakeWordEnabled != nextWw) {
        m_wakeWordEnabled = nextWw;
        Q_EMIT wakeWordEnabledChanged();
    }

    const QString nextSpeechMode = m_iface->property("speechOutputMode").toString();
    if (!nextSpeechMode.isEmpty() && m_speechOutputMode != nextSpeechMode) {
        m_speechOutputMode = nextSpeechMode;
        Q_EMIT speechOutputModeChanged();
    }

    const QVariant vadVar = m_iface->property("vadEnabled");
    if (vadVar.isValid()) {
        const bool nextVad = vadVar.toBool();
        if (m_vadEnabled != nextVad) {
            m_vadEnabled = nextVad;
            Q_EMIT vadEnabledChanged();
        }
    }

    const QVariant earconsVar = m_iface->property("earconsEnabled");
    if (earconsVar.isValid()) {
        const bool nextEarcons = earconsVar.toBool();
        if (m_earconsEnabled != nextEarcons) {
            m_earconsEnabled = nextEarcons;
            Q_EMIT earconsEnabledChanged();
        }
    }

    const QString nextModel = m_iface->property("ollamaModel").toString();
    if (!nextModel.isEmpty() && m_ollamaModel != nextModel) {
        m_ollamaModel = nextModel;
        Q_EMIT ollamaModelChanged();
    }

    const QVariant msgsVar = m_iface->property("messages");
    if (msgsVar.isValid() && msgsVar.canConvert<QVariantList>()) {
        const QVariantList rawMsgs = msgsVar.toList();
        QVariantList nextMsgs;
        nextMsgs.reserve(rawMsgs.size());
        for (const QVariant &item : rawMsgs) {
            nextMsgs.append(cleanDbusItem(item));
        }
        if (m_messages.size() != nextMsgs.size() || m_messages != nextMsgs) {
            m_messages = nextMsgs;
            Q_EMIT messagesChanged();
        }
    } else {
        loadLocalHistory();
    }
}

void KityClient::loadLocalHistory()
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity");
    const QString historyFile = dataDir + QStringLiteral("/history.json");
    QFile f(historyFile);
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        m_messages = doc.array().toVariantList();
        Q_EMIT messagesChanged();
    }
}

void KityClient::setWakeWordEnabled(bool enabled)
{
    if (m_wakeWordEnabled != enabled) {
        m_wakeWordEnabled = enabled;
        if (m_iface && m_iface->isValid()) {
            m_iface->setProperty("wakeWordEnabled", enabled);
        }
        Q_EMIT wakeWordEnabledChanged();
    }
}

void KityClient::setSpeechOutputMode(const QString &mode)
{
    if (m_speechOutputMode != mode) {
        m_speechOutputMode = mode;
        if (m_iface && m_iface->isValid()) {
            m_iface->setProperty("speechOutputMode", mode);
        }
        Q_EMIT speechOutputModeChanged();
    }
}

void KityClient::setVadEnabled(bool enabled)
{
    if (m_vadEnabled != enabled) {
        m_vadEnabled = enabled;
        if (m_iface && m_iface->isValid()) {
            m_iface->setProperty("vadEnabled", enabled);
        }
        Q_EMIT vadEnabledChanged();
    }
}

void KityClient::setEarconsEnabled(bool enabled)
{
    if (m_earconsEnabled != enabled) {
        m_earconsEnabled = enabled;
        if (m_iface && m_iface->isValid()) {
            m_iface->setProperty("earconsEnabled", enabled);
        }
        Q_EMIT earconsEnabledChanged();
    }
}

void KityClient::setOllamaModel(const QString &model)
{
    if (m_ollamaModel != model) {
        m_ollamaModel = model;
        if (m_iface && m_iface->isValid()) {
            m_iface->setProperty("ollamaModel", model);
            m_iface->asyncCall(QStringLiteral("setOllamaModel"), model);
        }
        QSettings settings(QStringLiteral("cesar"), QStringLiteral("kity"));
        settings.setValue(QStringLiteral("ollamaModel"), model);
        Q_EMIT ollamaModelChanged();
    }
}

void KityClient::sendMessage(const QString &text, bool fromVoice)
{
    if (text.trimmed().isEmpty()) {
        return;
    }
    if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("sendMessage"), text, fromVoice);
    }
}

void KityClient::startRecording()
{
    if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("startRecording"));
    }
}

void KityClient::stopRecording()
{
    if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("stopRecording"));
    }
}

void KityClient::toggleRecording()
{
    if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("toggleRecording"));
    }
}

void KityClient::stopSpeaking()
{
    if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("stopSpeaking"));
    }
}

void KityClient::stopGeneration()
{
    if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("stopGeneration"));
    }
}

void KityClient::clearHistory()
{
    if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("clearHistory"));
    }
    m_messages.clear();
    Q_EMIT messagesChanged();
}

void KityClient::checkReadiness()
{
    if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("checkReadiness"));
    }
}

void KityClient::refreshModels()
{
    if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("refreshModels"));
    }
}

void KityClient::copyToClipboard(const QString &text) const
{
    QClipboard *cb = QGuiApplication::clipboard();
    if (cb) {
        cb->setText(text, QClipboard::Clipboard);
        cb->setText(text, QClipboard::Selection);
    }
}

void KityClient::executeCommandInKonsole(const QString &command)
{
    const QString konsole = QStandardPaths::findExecutable(QStringLiteral("konsole"));
    if (!konsole.isEmpty()) {
        QProcess::startDetached(konsole, QStringList() << QStringLiteral("-e") << QStringLiteral("bash") << QStringLiteral("-c") << QStringLiteral("%1; exec bash").arg(command));
    } else if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("executeCommandInKonsole"), command);
    }
}

void KityClient::openKnowledgeFolder() const
{
    if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("openKnowledgeFolder"));
        return;
    }
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity/knowledge");
    QDir().mkpath(path);
    QProcess::startDetached(QStringLiteral("dolphin"), QStringList() << path);
}

void KityClient::openSettings() const
{
    QProcess::startDetached(QStringLiteral("systemsettings"), QStringList() << QStringLiteral("kcm_kity"));
}

void KityClient::runVisualIntelligence(const QString &captureMode, const QString &action, const QString &customQuestion)
{
    if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("runVisualIntelligence"), captureMode, action, customQuestion);
    }
}

void KityClient::applyWritingTool(const QString &action, const QString &customSourceText)
{
    if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("applyWritingTool"), action, customSourceText);
    }
}

QStringList KityClient::evaluateMathNotes(const QString &text) const
{
    if (m_iface && m_iface->isValid()) {
        QDBusReply<QStringList> reply = m_iface->call(QStringLiteral("evaluateMathNotes"), text);
        if (reply.isValid()) {
            return reply.value();
        }
    }
    return QStringList();
}

QString KityClient::renderMarkdown(const QString &markdown) const
{
    QString normalized = markdown;
    // Prevent indented bullets ("    • " or "    - ") from being parsed as non-wrapping <pre> blocks
    static const QRegularExpression indentedBulletRe(
        QStringLiteral("(?m)^[ \\t]{2,}([•\\-\\*]\\s+)"));
    normalized.replace(indentedBulletRe, QStringLiteral("- "));

    QTextDocument doc;
    doc.setMarkdown(normalized);
    QString html = doc.toHtml();
    // Ensure <pre> blocks wrap instead of overflowing horizontally
    html.replace(QStringLiteral("white-space: pre;"), QStringLiteral("white-space: pre-wrap; word-wrap: break-word;"));
    html.replace(QStringLiteral("white-space:pre;"), QStringLiteral("white-space: pre-wrap; word-wrap: break-word;"));
    return html;
}

void KityClient::speakText(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return;
    }
    if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("speakText"), text);
        return;
    }

    // Standalone direct TTS synthesis (Edge Neural TTS + PipeWire)
    const QString edgeCli = QStandardPaths::findExecutable(QStringLiteral("kity-edge-tts"));
    const QString pwPlay = QStandardPaths::findExecutable(QStringLiteral("pw-play"));
    if (edgeCli.isEmpty() || pwPlay.isEmpty()) {
        return;
    }

    auto resolveEdgeVoiceForOrb = [](const QString &accent, int idx) -> QString {
        if (accent.contains(QLatin1String("Latin"), Qt::CaseInsensitive)) {
            static const QStringList voices = {
                QStringLiteral("es-MX-DaliaNeural"), QStringLiteral("es-MX-JorgeNeural"),
                QStringLiteral("es-CO-SalomeNeural"), QStringLiteral("es-MX-BeatrizNeural"),
                QStringLiteral("es-AR-ElenaNeural"), QStringLiteral("es-CL-CatalinaNeural"),
                QStringLiteral("es-US-PalomaNeural")
            };
            return voices.value(idx, voices.at(3));
        }
        if (accent.contains(QLatin1String("American"), Qt::CaseInsensitive)) {
            static const QStringList voices = {
                QStringLiteral("en-US-AvaNeural"), QStringLiteral("en-US-AndrewNeural"),
                QStringLiteral("en-US-EmmaNeural"), QStringLiteral("en-US-AriaNeural"),
                QStringLiteral("en-US-BrianNeural"), QStringLiteral("en-US-JennyNeural"),
                QStringLiteral("en-US-GuyNeural")
            };
            return voices.value(idx, voices.at(3));
        }
        if (accent.contains(QLatin1String("British"), Qt::CaseInsensitive)) {
            static const QStringList voices = {
                QStringLiteral("en-GB-SoniaNeural"), QStringLiteral("en-GB-RyanNeural"),
                QStringLiteral("en-GB-LibbyNeural"), QStringLiteral("en-GB-MaisieNeural"),
                QStringLiteral("en-GB-ThomasNeural"), QStringLiteral("en-GB-AbbiNeural"),
                QStringLiteral("en-GB-OllieNeural")
            };
            return voices.value(idx, voices.at(3));
        }
        static const QStringList esVoices = {
            QStringLiteral("es-ES-AlvaroNeural"), QStringLiteral("es-ES-AbrilNeural"),
            QStringLiteral("es-ES-ArnauNeural"), QStringLiteral("es-ES-ElviraNeural"),
            QStringLiteral("es-ES-EstrellaNeural"), QStringLiteral("es-ES-DarioNeural"),
            QStringLiteral("es-ES-XimenaNeural")
        };
        return esVoices.value(idx, esVoices.at(3));
    };

    const QString voice = resolveEdgeVoiceForOrb(m_voiceAccent, qBound(0, m_voiceIndex, 6));
    const int ratePct = qRound((m_voicePace - 0.5) * 80.0);
    const int pitchHz = qRound((m_voiceExpressivity - 0.5) * 28.0);
    const QString rateArg = QStringLiteral("--rate=%1%2%").arg(ratePct >= 0 ? QStringLiteral("+") : QString()).arg(ratePct);
    const QString pitchArg = QStringLiteral("--pitch=%1%2Hz").arg(pitchHz >= 0 ? QStringLiteral("+") : QString()).arg(pitchHz);

    auto *ttsProc = new QProcess(this);
    auto *playProc = new QProcess(this);
    ttsProc->setStandardOutputProcess(playProc);
    connect(playProc, &QProcess::finished, this, [ttsProc, playProc](int, QProcess::ExitStatus) {
        playProc->deleteLater();
        ttsProc->deleteLater();
    });
    ttsProc->start(edgeCli, {QStringLiteral("--voice"), voice, rateArg, pitchArg, QStringLiteral("--text"), text, QStringLiteral("--write-media"), QStringLiteral("-")});
    playProc->start(pwPlay, {QStringLiteral("-")});
}

QStringList KityClient::extractTerminalCommands(const QString &markdown) const
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

void KityClient::controlMedia(const QString &action)
{
    if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("controlMedia"), action);
    } else {
        const QString act = action.trimmed().toLower();
        if (act == QStringLiteral("play-pause")) {
            QProcess::execute(QStringLiteral("playerctl"), {QStringLiteral("play-pause")});
        } else if (act == QStringLiteral("next")) {
            QProcess::execute(QStringLiteral("playerctl"), {QStringLiteral("next")});
        } else if (act == QStringLiteral("previous")) {
            QProcess::execute(QStringLiteral("playerctl"), {QStringLiteral("previous")});
        }
    }
}

void KityClient::openKOrganizer() const
{
    if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("openKOrganizer"));
    } else {
        const QString korganizer = QStandardPaths::findExecutable(QStringLiteral("korganizer"));
        if (!korganizer.isEmpty()) {
            QProcess::startDetached(korganizer, QStringList());
        }
    }
}

QString KityClient::mobilePairingUrl() const
{
    if (!m_iface || !m_iface->isValid()) {
        return QString();
    }
    return m_iface->property("mobilePairingUrl").toString();
}

bool KityClient::shareMobilePairingWithKdeConnect(const QString &deviceId) const
{
    if (!m_iface || !m_iface->isValid()) {
        copyToClipboard(mobilePairingUrl());
        return false;
    }
    QDBusReply<bool> reply = m_iface->call(QStringLiteral("shareMobilePairingWithKdeConnect"), deviceId);
    if (!reply.isValid() || !reply.value()) {
        copyToClipboard(mobilePairingUrl());
    }
    return reply.isValid() ? reply.value() : false;
}

// =========================================================================
// macOS 27 Golden Gate: Conversations & Voice Settings
// =========================================================================

void KityClient::loadConversations()
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity");
    QDir().mkpath(dataDir);
    const QString convFile = dataDir + QStringLiteral("/conversations.json");
    QFile f(convFile);
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isArray()) {
            m_conversations = doc.array().toVariantList();
        }
    }

    if (m_conversations.isEmpty()) {
        // Seed initial Golden Gate conversations matching "The Siri app" screenshot
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        auto makeConv = [nowMs](const QString &id, const QString &timeLabel, const QString &title, const QString &preview, bool pinned, const QVariantList &msgs) {
            QVariantMap map;
            map[QStringLiteral("id")] = id;
            map[QStringLiteral("timeLabel")] = timeLabel;
            map[QStringLiteral("title")] = title;
            map[QStringLiteral("preview")] = preview;
            map[QStringLiteral("pinned")] = pinned;
            map[QStringLiteral("updatedAtMs")] = nowMs;
            map[QStringLiteral("messages")] = msgs;
            return map;
        };

        QVariantList siriAiMsgs;
        {
            QVariantMap u;
            u[QStringLiteral("role")] = QStringLiteral("user");
            u[QStringLiteral("content")] = QStringLiteral("Tell me what's new with Kity AI in Golden Gate");
            siriAiMsgs.append(u);

            QVariantMap a;
            a[QStringLiteral("role")] = QStringLiteral("assistant");
            a[QStringLiteral("content")] = QStringLiteral(
                "### Writing Tools\n"
                "Kity can now generate text drafts from scratch, shorten content, or turn bullet points into prose. "
                "It also provides automatic proofreading for spelling and grammar as you type across the system.\n\n"
                "### Dedicated Kity App\n"
                "For the first time, Kity has a standalone app that acts as a conversational chatbot with a full conversation sidebar, "
                "allowing you to look up past answers and continue older conversations with context intact.\n\n"
                "### Voice and Dictation\n"
                "The assistant features a brand-new voice experience that is more conversational and detailed. "
                "You can customize Kity's expressiveness and speaking pace to suit your preferences.");
            QVariantList sources;
            QVariantMap s1; s1[QStringLiteral("title")] = QStringLiteral("CachyOS Intelligence +2"); s1[QStringLiteral("url")] = QStringLiteral("https://cachyos.org");
            sources.append(s1);
            a[QStringLiteral("sources")] = sources;
            siriAiMsgs.append(a);
        }

        m_conversations.append(makeConv(
            QStringLiteral("conv-siri-ai"),
            QStringLiteral("7:05 AM"),
            QStringLiteral("Kity AI"),
            QStringLiteral("Kity recently introduced a completely rebuilt digital assistant powered by the next generation of local & cloud intelligence..."),
            true,
            m_messages.isEmpty() ? siriAiMsgs : m_messages));

        m_conversations.append(makeConv(
            QStringLiteral("conv-features"),
            QStringLiteral("Sunday"),
            QStringLiteral("Golden Gate Features"),
            QStringLiteral("In macOS Golden Gate, Liquid Glass introduces a continuous opacity slider, edge-to-edge sidebars, and bouncy stoplights..."),
            false,
            QVariantList()));

        m_conversations.append(makeConv(
            QStringLiteral("conv-notes"),
            QStringLiteral("Sunday"),
            QStringLiteral("Markdown in Notes"),
            QStringLiteral("I've exported your note into clean Markdown format with horizontal dividers..."),
            false,
            QVariantList()));

        m_conversations.append(makeConv(
            QStringLiteral("conv-system"),
            QStringLiteral("Sunday"),
            QStringLiteral("System Status"),
            QStringLiteral("Kernel BORE and Wayland compositor running at < 1ms frame latency..."),
            false,
            QVariantList()));

        saveConversations();
    }

    // Apply Conversation Retention policy ("forever", "1year", "30days")
    if (m_historyRetention == QLatin1String("30days") || m_historyRetention == QLatin1String("1year")) {
        const qint64 maxAgeMs = (m_historyRetention == QLatin1String("30days"))
            ? (30LL * 24LL * 3600LL * 1000LL)
            : (365LL * 24LL * 3600LL * 1000LL);
        const qint64 cutoffMs = QDateTime::currentMSecsSinceEpoch() - maxAgeMs;
        QVariantList filtered;
        for (const QVariant &v : std::as_const(m_conversations)) {
            const QVariantMap map = v.toMap();
            const bool pinned = map.value(QStringLiteral("pinned")).toBool();
            const qint64 updatedMs = map.value(QStringLiteral("updatedAtMs"), QDateTime::currentMSecsSinceEpoch()).toLongLong();
            if (pinned || updatedMs >= cutoffMs) {
                filtered.append(map);
            }
        }
        if (filtered.size() != m_conversations.size() && !filtered.isEmpty()) {
            m_conversations = filtered;
            saveConversations();
        }
    }

    // Apply "Open Kity App To" setting ("past" vs "new")
    if (m_openToMode == QLatin1String("new")) {
        newConversation();
        return;
    }

    if (!m_conversations.isEmpty()) {
        m_activeConversationId = m_conversations.first().toMap().value(QStringLiteral("id")).toString();
        if (m_messages.isEmpty()) {
            m_messages = m_conversations.first().toMap().value(QStringLiteral("messages")).toList();
            Q_EMIT messagesChanged();
        }
    }
    Q_EMIT conversationsChanged();
    Q_EMIT activeConversationIdChanged();
}

void KityClient::saveConversations()
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity");
    QDir().mkpath(dataDir);
    const QString convFile = dataDir + QStringLiteral("/conversations.json");
    QFile f(convFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(QJsonArray::fromVariantList(m_conversations)).toJson(QJsonDocument::Compact));
    }
}

void KityClient::syncActiveConversationFromMessages()
{
    if (m_messages.isEmpty() || m_activeConversationId.isEmpty()) {
        return;
    }

    QString firstUserPrompt;
    QString lastAssistantReply;
    for (const QVariant &v : std::as_const(m_messages)) {
        const QVariantMap msg = v.toMap();
        const QString role = msg.value(QStringLiteral("role")).toString();
        const QString content = msg.value(QStringLiteral("content")).toString().trimmed();
        if (role == QLatin1String("user") && firstUserPrompt.isEmpty()) {
            firstUserPrompt = content.left(32);
        } else if (role == QLatin1String("assistant") && !content.isEmpty()) {
            lastAssistantReply = content;
        }
    }

    for (int i = 0; i < m_conversations.size(); ++i) {
        QVariantMap map = m_conversations[i].toMap();
        if (map.value(QStringLiteral("id")).toString() == m_activeConversationId) {
            if (!firstUserPrompt.isEmpty() && map.value(QStringLiteral("title")).toString() == QLatin1String("New Conversation")) {
                map[QStringLiteral("title")] = firstUserPrompt;
            }
            if (!lastAssistantReply.isEmpty()) {
                QString cleanPreview = lastAssistantReply;
                cleanPreview.remove(QRegularExpression(QStringLiteral("[#*`>]+")));
                map[QStringLiteral("preview")] = cleanPreview.simplified().left(180);
            }
            map[QStringLiteral("timeLabel")] = QDateTime::currentDateTime().toString(QStringLiteral("h:mm AP"));
            map[QStringLiteral("updatedAtMs")] = QDateTime::currentMSecsSinceEpoch();
            map[QStringLiteral("messages")] = m_messages;
            m_conversations[i] = map;
            saveConversations();
            Q_EMIT conversationsChanged();
            break;
        }
    }
}

void KityClient::newConversation()
{
    clearHistory();
    const QString newId = QStringLiteral("conv-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    QVariantMap map;
    map[QStringLiteral("id")] = newId;
    map[QStringLiteral("timeLabel")] = QDateTime::currentDateTime().toString(QStringLiteral("h:mm AP"));
    map[QStringLiteral("title")] = QStringLiteral("New Conversation");
    map[QStringLiteral("preview")] = QStringLiteral("Ask Kity anything or start speaking...");
    map[QStringLiteral("pinned")] = false;
    map[QStringLiteral("updatedAtMs")] = QDateTime::currentMSecsSinceEpoch();
    map[QStringLiteral("messages")] = QVariantList();
    m_conversations.prepend(map);
    m_activeConversationId = newId;
    saveConversations();
    Q_EMIT conversationsChanged();
    Q_EMIT activeConversationIdChanged();
}

void KityClient::selectConversation(const QString &id)
{
    if (m_activeConversationId == id) {
        return;
    }
    for (const QVariant &v : std::as_const(m_conversations)) {
        const QVariantMap map = v.toMap();
        if (map.value(QStringLiteral("id")).toString() == id) {
            m_activeConversationId = id;
            m_messages = map.value(QStringLiteral("messages")).toList();
            Q_EMIT activeConversationIdChanged();
            Q_EMIT messagesChanged();
            return;
        }
    }
}

void KityClient::togglePinConversation(const QString &id)
{
    for (int i = 0; i < m_conversations.size(); ++i) {
        QVariantMap map = m_conversations[i].toMap();
        if (map.value(QStringLiteral("id")).toString() == id) {
            map[QStringLiteral("pinned")] = !map.value(QStringLiteral("pinned")).toBool();
            m_conversations[i] = map;
            saveConversations();
            Q_EMIT conversationsChanged();
            return;
        }
    }
}

void KityClient::deleteConversation(const QString &id)
{
    for (int i = 0; i < m_conversations.size(); ++i) {
        if (m_conversations[i].toMap().value(QStringLiteral("id")).toString() == id) {
            m_conversations.removeAt(i);
            saveConversations();
            if (m_activeConversationId == id && !m_conversations.isEmpty()) {
                selectConversation(m_conversations.first().toMap().value(QStringLiteral("id")).toString());
            }
            Q_EMIT conversationsChanged();
            return;
        }
    }
}

void KityClient::launchSpotlightCategory(const QString &category)
{
    if (category == QLatin1String("applications")) {
        QProcess::startDetached(QStringLiteral("qdbus6"), {QStringLiteral("org.kde.krunner"), QStringLiteral("/App"), QStringLiteral("org.kde.krunner.App.display")});
    } else if (category == QLatin1String("files")) {
        QProcess::startDetached(QStringLiteral("dolphin"), {});
    } else if (category == QLatin1String("actions")) {
        sendMessage(QStringLiteral("Muéstrame las acciones rápidas del sistema que puedes ejecutar por mí."), false);
    } else if (category == QLatin1String("clipboard")) {
        const QString clip = QGuiApplication::clipboard() ? QGuiApplication::clipboard()->text() : QString();
        if (!clip.trimmed().isEmpty()) {
            sendMessage(QStringLiteral("Analiza o resume este contenido de mi portapapeles:\n\n") + clip.left(2000), false);
        } else {
            sendMessage(QStringLiteral("Mi portapapeles está vacío ahora mismo. ¿Qué puedes hacer con lo que copie?"), false);
        }
    }
}

void KityClient::previewSelectedVoice()
{
    if (m_voiceAccent.contains(QLatin1String("American"), Qt::CaseInsensitive)
        || m_voiceAccent.contains(QLatin1String("British"), Qt::CaseInsensitive)) {
        speakText(QStringLiteral("Hello, I'm Kity in macOS Golden Gate. You can customize my voice, pace, and expressivity."));
    } else {
        speakText(QStringLiteral("Hola, soy Kity en macOS Golden Gate. Puedes ajustar mi voz, ritmo y expresividad a tu gusto."));
    }
}

static void writeGoldenGateConfigAndNotify(const QString &key, const QVariant &value, QDBusInterface *iface)
{
    KSharedConfig::Ptr kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    KConfigGroup cfg = kityrc->group(QStringLiteral("General"));
    cfg.writeEntry(key, value);
    cfg.sync();
    if (iface && iface->isValid()) {
        iface->asyncCall(QStringLiteral("reloadConfig"));
    }
}

void KityClient::setSidebarViewMode(const QString &mode)
{
    if (m_sidebarViewMode != mode) {
        m_sidebarViewMode = mode;
        writeGoldenGateConfigAndNotify(QStringLiteral("sidebarViewMode"), mode, m_iface);
        Q_EMIT goldenGatePrefsChanged();
    }
}

void KityClient::setSiriMode(const QString &mode)
{
    if (m_siriMode != mode) {
        m_siriMode = mode;
        writeGoldenGateConfigAndNotify(QStringLiteral("siriMode"), mode, m_iface);
        Q_EMIT goldenGatePrefsChanged();
    }
}

void KityClient::setVoiceAccent(const QString &accent)
{
    if (m_voiceAccent != accent) {
        m_voiceAccent = accent;
        writeGoldenGateConfigAndNotify(QStringLiteral("voiceAccent"), accent, m_iface);
        Q_EMIT goldenGatePrefsChanged();
    }
}

void KityClient::setVoiceIndex(int index)
{
    const int clamped = qBound(0, index, 6);
    if (m_voiceIndex != clamped) {
        m_voiceIndex = clamped;
        writeGoldenGateConfigAndNotify(QStringLiteral("voiceIndex"), clamped, m_iface);
        Q_EMIT goldenGatePrefsChanged();
    }
}

void KityClient::setVoicePace(qreal pace)
{
    const qreal clamped = qBound(0.0, pace, 1.0);
    if (!qFuzzyCompare(m_voicePace, clamped)) {
        m_voicePace = clamped;
        writeGoldenGateConfigAndNotify(QStringLiteral("voicePace"), clamped, m_iface);
        Q_EMIT goldenGatePrefsChanged();
    }
}

void KityClient::setVoiceExpressivity(qreal expr)
{
    const qreal clamped = qBound(0.0, expr, 1.0);
    if (!qFuzzyCompare(m_voiceExpressivity, clamped)) {
        m_voiceExpressivity = clamped;
        writeGoldenGateConfigAndNotify(QStringLiteral("voiceExpressivity"), clamped, m_iface);
        Q_EMIT goldenGatePrefsChanged();
    }
}

void KityClient::setHistoryRetention(const QString &ret)
{
    if (m_historyRetention != ret) {
        m_historyRetention = ret;
        writeGoldenGateConfigAndNotify(QStringLiteral("historyRetention"), ret, m_iface);
        loadConversations();
        Q_EMIT goldenGatePrefsChanged();
    }
}

void KityClient::setPreviewLines(int lines)
{
    const int clamped = qBound(1, lines, 8);
    if (m_previewLines != clamped) {
        m_previewLines = clamped;
        writeGoldenGateConfigAndNotify(QStringLiteral("previewLines"), clamped, m_iface);
        Q_EMIT goldenGatePrefsChanged();
    }
}

void KityClient::setOpenToMode(const QString &mode)
{
    if (m_openToMode != mode) {
        m_openToMode = mode;
        writeGoldenGateConfigAndNotify(QStringLiteral("openToMode"), mode, m_iface);
        Q_EMIT goldenGatePrefsChanged();
    }
}

void KityClient::setTextScale(qreal scale)
{
    const qreal clamped = qBound(0.85, scale, 1.35);
    if (!qFuzzyCompare(m_textScale, clamped)) {
        m_textScale = clamped;
        writeGoldenGateConfigAndNotify(QStringLiteral("textScale"), clamped, m_iface);
        Q_EMIT goldenGatePrefsChanged();
    }
}
