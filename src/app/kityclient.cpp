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
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTextDocument>

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

    QSettings settings(QStringLiteral("cesar"), QStringLiteral("kity"));
    m_ollamaModel = settings.value(QStringLiteral("ollamaModel"), QStringLiteral("llama3.2:3b")).toString();
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
    QTextDocument doc;
    doc.setMarkdown(markdown);
    return doc.toHtml();
}

void KityClient::speakText(const QString &text)
{
    if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("speakText"), text);
    }
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
