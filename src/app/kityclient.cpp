/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "kityclient.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTextDocument>

KityClient::KityClient(QObject *parent)
    : QObject(parent)
    , m_watcher(new QDBusServiceWatcher(QStringLiteral("org.cesar.kity"),
                                        QDBusConnection::sessionBus(),
                                        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration,
                                        this))
{
    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered, this, &KityClient::onServiceRegistered);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, &KityClient::onServiceUnregistered);

    setupDBus();
    loadLocalHistory();
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
            // Fallback to class-name interface if default is used
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

    const QVariant msgsVar = m_iface->property("messages");
    if (msgsVar.isValid() && msgsVar.canConvert<QVariantList>()) {
        const QVariantList nextMsgs = msgsVar.toList();
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
    if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("executeCommandInKonsole"), command);
    }
}

void KityClient::openKnowledgeFolder() const
{
    if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("openKnowledgeFolder"));
    }
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
