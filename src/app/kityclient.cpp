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
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTextDocument>
#include <QUrl>
#include <QUuid>
#include <KSharedConfig>
#include <KConfigGroup>
#include <KNotification>
#include <KLocalizedString>

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

    m_convManager = std::make_unique<Kity::ConversationManager>(this);
    m_conversations = m_convManager->loadConversations(m_historyRetention, m_openToMode, m_activeConversationId, m_messages);
    connect(this, &KityClient::messagesChanged, this, &KityClient::syncActiveConversationFromMessages);

    auto *cfgWatcher = new QFileSystemWatcher(this);
    const QString cfgDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString cfgFile = cfgDir + QStringLiteral("/kityrc");
    if (!cfgDir.isEmpty()) {
        cfgWatcher->addPath(cfgDir);
    }
    if (QFile::exists(cfgFile)) {
        cfgWatcher->addPath(cfgFile);
    }
    auto reloadKityrc = [this, cfgWatcher, cfgFile]() {
        if (QFile::exists(cfgFile) && !cfgWatcher->files().contains(cfgFile)) {
            cfgWatcher->addPath(cfgFile);
        }
        KSharedConfig::Ptr rc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
        rc->reparseConfiguration();
        KConfigGroup g = rc->group(QStringLiteral("General"));
        m_ollamaModel = g.readEntry(QStringLiteral("ollamaModel"), m_ollamaModel);
        m_siriMode = g.readEntry(QStringLiteral("siriMode"), m_siriMode);
        m_voiceAccent = g.readEntry(QStringLiteral("voiceAccent"), m_voiceAccent);
        m_voiceIndex = g.readEntry(QStringLiteral("voiceIndex"), m_voiceIndex);
        m_voicePace = g.readEntry(QStringLiteral("voicePace"), m_voicePace);
        m_voiceExpressivity = g.readEntry(QStringLiteral("voiceExpressivity"), m_voiceExpressivity);
        const QString newRetention = g.readEntry(QStringLiteral("historyRetention"), m_historyRetention);
        m_previewLines = g.readEntry(QStringLiteral("previewLines"), m_previewLines);
        m_openToMode = g.readEntry(QStringLiteral("openToMode"), m_openToMode);
        if (newRetention != m_historyRetention) {
            setHistoryRetention(newRetention);
        }
        Q_EMIT goldenGatePrefsChanged();
    };
    connect(cfgWatcher, &QFileSystemWatcher::fileChanged, this, reloadKityrc);
    connect(cfgWatcher, &QFileSystemWatcher::directoryChanged, this, reloadKityrc);
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

            bus.connect(QStringLiteral("org.cesar.kity"),
                        QStringLiteral("/Kity"),
                        m_iface->interface(),
                        QStringLiteral("toggleRequested"),
                        this,
                        SIGNAL(toggleRequested()));

            bus.connect(QStringLiteral("org.cesar.kity"),
                        QStringLiteral("/Kity"),
                        m_iface->interface(),
                        QStringLiteral("openRequested"),
                        this,
                        SIGNAL(openRequested()));

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
    // Also use wl-copy to ensure persistent Wayland selection ownership
    QProcess::startDetached(QStringLiteral("wl-copy"), {text});
    QProcess::startDetached(QStringLiteral("wl-copy"), {QStringLiteral("-p"), text});
}

void KityClient::replaceText(const QString &text) const
{
    copyToClipboard(text);
    KNotification::event(QStringLiteral("notification"),
                         i18n("Herramientas de Escritura"),
                         i18n("Texto copiado al portapapeles. Listo para pegar (Ctrl+V) en tu aplicación."),
                         QStringLiteral("org.cesar.kity"));
}

QString KityClient::readWaylandSelection() const
{
    QProcess process;
    process.start(QStringLiteral("wl-paste"), {QStringLiteral("-p"), QStringLiteral("--no-newline")});
    if (process.waitForFinished(800)) {
        const QString text = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        if (!text.isEmpty()) {
            return text;
        }
    }
    process.start(QStringLiteral("wl-paste"), {QStringLiteral("--no-newline")});
    if (process.waitForFinished(800)) {
        const QString text = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        if (!text.isEmpty()) {
            return text;
        }
    }
    QClipboard *cb = QGuiApplication::clipboard();
    if (cb) {
        QString text = cb->text(QClipboard::Selection).trimmed();
        if (text.isEmpty()) text = cb->text(QClipboard::Clipboard).trimmed();
        return text;
    }
    return QString();
}

void KityClient::executeCommandInKonsole(const QString &command)
{
    const QString konsole = QStandardPaths::findExecutable(QStringLiteral("konsole"));
    if (!konsole.isEmpty()) {
        QString cmd = command.trimmed();
        if (cmd.contains(QStringLiteral("apt update")) || cmd.contains(QStringLiteral("apt-get update"))
            || cmd.contains(QStringLiteral("apt upgrade")) || cmd.contains(QStringLiteral("apt-get upgrade"))) {
            cmd = QStringLiteral("sudo pacman -Syu");
        }

        const bool isUpdate = cmd.contains(QStringLiteral("pacman -S"), Qt::CaseInsensitive)
                           || cmd.contains(QStringLiteral("pacman -Syu"), Qt::CaseInsensitive)
                           || cmd.contains(QStringLiteral("pacman -Syyu"), Qt::CaseInsensitive)
                           || cmd.contains(QStringLiteral("paru"), Qt::CaseInsensitive)
                           || cmd.contains(QStringLiteral("yay"), Qt::CaseInsensitive)
                           || cmd.contains(QStringLiteral("flatpak update"), Qt::CaseInsensitive);

        if (isUpdate) {
            const QString wrappedCmd = QStringLiteral(
                "%1; RET=$?; "
                "if [ $RET -eq 0 ]; then "
                    "echo -e \"\\n\\033[1;32m✓ Proceso finalizado con éxito.\\033[0m Cerrando terminal en 4 segundos (pulsa cualquier tecla para cerrar de inmediato)...\"; "
                    "read -t 4 -n 1 -s -r; "
                    "exit 0; "
                "else "
                    "echo -e \"\\n\\033[1;31m✗ El proceso no se completó o fue cancelado (código $RET).\\033[0m\"; "
                    "read -n 1 -s -r -p \"Presiona cualquier tecla para cerrar la ventana...\"; "
                    "exit $RET; "
                "fi"
            ).arg(cmd);
            QProcess::startDetached(konsole, QStringList() << QStringLiteral("-p") << QStringLiteral("tabtitle=Actualización del Sistema") << QStringLiteral("-e") << QStringLiteral("bash") << QStringLiteral("-c") << wrappedCmd);
        } else {
            QProcess::startDetached(konsole, QStringList() << QStringLiteral("-e") << QStringLiteral("bash") << QStringLiteral("-c") << QStringLiteral("%1; exec bash").arg(cmd));
        }
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
    return Kity::MarkdownFormatter::renderMarkdown(markdown);
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

    int orbPitchShift = 0;
    const QString voice = resolveEdgeVoiceForOrb(m_voiceAccent, qBound(0, m_voiceIndex, 6), orbPitchShift);
    const int ratePct = qRound((m_voicePace - 0.5) * 80.0);
    const int userPitchHz = qRound((m_voiceExpressivity - 0.5) * 28.0);
    const int totalPitchHz = userPitchHz + orbPitchShift;
    const QString rateArg = QStringLiteral("--rate=%1%2%").arg(ratePct >= 0 ? QStringLiteral("+") : QString()).arg(ratePct);
    const QString pitchArg = QStringLiteral("--pitch=%1%2Hz").arg(totalPitchHz >= 0 ? QStringLiteral("+") : QString()).arg(totalPitchHz);

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
    return Kity::MarkdownFormatter::extractTerminalCommands(markdown);
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

void KityClient::sendPendingEmail()
{
    if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("sendPendingEmail"));
    }
}

void KityClient::openPendingEmailInProton() const
{
    if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("openPendingEmailInProton"));
    }
}

void KityClient::discardPendingEmail()
{
    if (m_iface && m_iface->isValid()) {
        m_iface->asyncCall(QStringLiteral("discardPendingEmail"));
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

void KityClient::syncActiveConversationFromMessages()
{
    if (m_convManager) {
        m_convManager->syncActiveFromMessages(m_conversations, m_activeConversationId, m_messages);
        Q_EMIT conversationsChanged();
    }
}

void KityClient::newConversation()
{
    clearHistory();
    if (m_convManager) {
        m_activeConversationId = m_convManager->createNewConversation(m_conversations);
        Q_EMIT conversationsChanged();
        Q_EMIT activeConversationIdChanged();
    }
}

void KityClient::selectConversation(const QString &id)
{
    if (m_activeConversationId == id || !m_convManager) {
        return;
    }
    if (m_convManager->selectConversation(m_conversations, id, m_messages)) {
        m_activeConversationId = id;
        Q_EMIT activeConversationIdChanged();
        Q_EMIT messagesChanged();
    }
}

void KityClient::togglePinConversation(const QString &id)
{
    if (m_convManager) {
        m_convManager->togglePin(m_conversations, id);
        Q_EMIT conversationsChanged();
    }
}

void KityClient::renameConversation(const QString &id, const QString &newTitle)
{
    if (m_convManager) {
        m_convManager->rename(m_conversations, id, newTitle);
        Q_EMIT conversationsChanged();
    }
}

void KityClient::deleteConversation(const QString &id)
{
    deleteConversations(QStringList{id});
}

void KityClient::deleteConversations(const QStringList &ids)
{
    if (m_convManager) {
        m_convManager->deleteConversations(m_conversations, ids, m_activeConversationId, m_messages);
        Q_EMIT conversationsChanged();
        Q_EMIT activeConversationIdChanged();
        Q_EMIT messagesChanged();
    }
}

void KityClient::attachFiles(const QStringList &fileUrls)
{
    if (m_convManager) {
        const QString prompt = m_convManager->processAttachedFiles(fileUrls);
        if (!prompt.isEmpty()) {
            sendMessage(prompt, false);
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
        if (m_convManager) {
            m_conversations = m_convManager->loadConversations(m_historyRetention, m_openToMode, m_activeConversationId, m_messages);
            Q_EMIT conversationsChanged();
            Q_EMIT activeConversationIdChanged();
        }
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
