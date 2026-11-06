/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "kcm_kity.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KPluginFactory>
#include <KSharedConfig>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>

K_PLUGIN_CLASS_WITH_JSON(KcmKity, "kcm_kity.json")

static const QString defaultSystemPrompt()
{
    return QStringLiteral(
        "Eres Kity, un asistente de voz e inteligencia artificial nativo para KDE Plasma en CachyOS Linux. "
        "Responde SIEMPRE en español de España (castellano), de forma concisa, clara, directa y natural, "
        "optimizada para síntesis de voz (TTS).");
}

KcmKity::KcmKity(QObject *parent, const KPluginMetaData &data)
    : KQuickConfigModule(parent, data)
    , m_nam(new QNetworkAccessManager(this))
    , m_systemPrompt(defaultSystemPrompt())
{
    setButtons(KAbstractConfigModule::Apply | KAbstractConfigModule::Default);
}

KcmKity::~KcmKity() = default;

void KcmKity::load()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    KConfigGroup cfg = config->group(QStringLiteral("General"));

    m_wakeWordEnabled = cfg.readEntry(QStringLiteral("wakeWordEnabled"), true);
    m_speechOutputMode = cfg.readEntry(QStringLiteral("speechOutputMode"), QStringLiteral("always"));
    m_earconsEnabled = cfg.readEntry(QStringLiteral("earconsEnabled"), true);
    m_vadEnabled = cfg.readEntry(QStringLiteral("vadEnabled"), true);
    m_vadSilenceTimeoutMs = cfg.readEntry(QStringLiteral("vadSilenceTimeoutMs"), 1300);
    m_autoListenOnActivate = cfg.readEntry(QStringLiteral("autoListenOnActivate"), false);
    m_whisperLanguage = cfg.readEntry(QStringLiteral("whisperLanguage"), QStringLiteral("es"));
    m_whisperModelPath = cfg.readEntry(QStringLiteral("whisperModelPath"), QString());
    m_piperVoicePath = cfg.readEntry(QStringLiteral("piperVoicePath"), QString());

    m_ollamaUrl = cfg.readEntry(QStringLiteral("ollamaUrl"), QStringLiteral("http://127.0.0.1:11434"));
    m_ollamaModel = cfg.readEntry(QStringLiteral("ollamaModel"), QStringLiteral("llama3.2:3b"));
    m_ollamaVisionModel = cfg.readEntry(QStringLiteral("ollamaVisionModel"), QStringLiteral("llama3.2:3b"));
    if (m_ollamaVisionModel == QLatin1String("moondream")) {
        m_ollamaVisionModel = QStringLiteral("llama3.2:3b");
    }
    m_ollamaTemperature = cfg.readEntry(QStringLiteral("ollamaTemperature"), 0.2);
    m_ollamaNumCtx = cfg.readEntry(QStringLiteral("ollamaNumCtx"), 4096);
    m_ollamaNumThread = cfg.readEntry(QStringLiteral("ollamaNumThread"), 6);
    m_ollamaNumPredict = cfg.readEntry(QStringLiteral("ollamaNumPredict"), 384);
    m_ollamaKeepAlive = cfg.readEntry(QStringLiteral("ollamaKeepAlive"), QStringLiteral("24h"));
    m_systemPrompt = cfg.readEntry(QStringLiteral("systemPrompt"), defaultSystemPrompt());

    m_knowledgeBaseEnabled = cfg.readEntry(QStringLiteral("knowledgeBaseEnabled"), true);
    m_knowledgeBaseMaxSnippets = cfg.readEntry(QStringLiteral("knowledgeBaseMaxSnippets"), 3);

    m_panelIconStyle = cfg.readEntry(QStringLiteral("panelIconStyle"), QStringLiteral("symbolic"));
    m_pin = cfg.readEntry(QStringLiteral("pin"), false);
    m_defaultEmailClient = cfg.readEntry(QStringLiteral("defaultEmailClient"), QStringLiteral("proton"));
    m_defaultCalendarClient = cfg.readEntry(QStringLiteral("defaultCalendarClient"), QStringLiteral("proton"));

    Q_EMIT wakeWordEnabledChanged();
    Q_EMIT speechOutputModeChanged();
    Q_EMIT earconsEnabledChanged();
    Q_EMIT vadEnabledChanged();
    Q_EMIT vadSilenceTimeoutMsChanged();
    Q_EMIT autoListenOnActivateChanged();
    Q_EMIT whisperLanguageChanged();
    Q_EMIT whisperModelPathChanged();
    Q_EMIT piperVoicePathChanged();

    Q_EMIT ollamaUrlChanged();
    Q_EMIT ollamaModelChanged();
    Q_EMIT ollamaVisionModelChanged();
    Q_EMIT ollamaTemperatureChanged();
    Q_EMIT ollamaNumCtxChanged();
    Q_EMIT ollamaNumThreadChanged();
    Q_EMIT ollamaNumPredictChanged();
    Q_EMIT ollamaKeepAliveChanged();
    Q_EMIT systemPromptChanged();

    Q_EMIT knowledgeBaseEnabledChanged();
    Q_EMIT knowledgeBaseMaxSnippetsChanged();

    Q_EMIT panelIconStyleChanged();
    Q_EMIT pinChanged();
    Q_EMIT defaultEmailClientChanged();
    Q_EMIT defaultCalendarClientChanged();

    setNeedsSave(false);

    checkReadiness();
    fetchModels();
}

void KcmKity::save()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    KConfigGroup cfg = config->group(QStringLiteral("General"));

    cfg.writeEntry(QStringLiteral("wakeWordEnabled"), m_wakeWordEnabled);
    cfg.writeEntry(QStringLiteral("speechOutputMode"), m_speechOutputMode);
    cfg.writeEntry(QStringLiteral("earconsEnabled"), m_earconsEnabled);
    cfg.writeEntry(QStringLiteral("vadEnabled"), m_vadEnabled);
    cfg.writeEntry(QStringLiteral("vadSilenceTimeoutMs"), m_vadSilenceTimeoutMs);
    cfg.writeEntry(QStringLiteral("autoListenOnActivate"), m_autoListenOnActivate);
    cfg.writeEntry(QStringLiteral("whisperLanguage"), m_whisperLanguage);
    cfg.writeEntry(QStringLiteral("whisperModelPath"), m_whisperModelPath);
    cfg.writeEntry(QStringLiteral("piperVoicePath"), m_piperVoicePath);

    cfg.writeEntry(QStringLiteral("ollamaUrl"), m_ollamaUrl);
    cfg.writeEntry(QStringLiteral("ollamaModel"), m_ollamaModel);
    cfg.writeEntry(QStringLiteral("ollamaVisionModel"), m_ollamaVisionModel);
    cfg.writeEntry(QStringLiteral("ollamaTemperature"), m_ollamaTemperature);
    cfg.writeEntry(QStringLiteral("ollamaNumCtx"), m_ollamaNumCtx);
    cfg.writeEntry(QStringLiteral("ollamaNumThread"), m_ollamaNumThread);
    cfg.writeEntry(QStringLiteral("ollamaNumPredict"), m_ollamaNumPredict);
    cfg.writeEntry(QStringLiteral("ollamaKeepAlive"), m_ollamaKeepAlive);
    cfg.writeEntry(QStringLiteral("systemPrompt"), m_systemPrompt);

    cfg.writeEntry(QStringLiteral("knowledgeBaseEnabled"), m_knowledgeBaseEnabled);
    cfg.writeEntry(QStringLiteral("knowledgeBaseMaxSnippets"), m_knowledgeBaseMaxSnippets);

    cfg.writeEntry(QStringLiteral("panelIconStyle"), m_panelIconStyle);
    cfg.writeEntry(QStringLiteral("pin"), m_pin);
    cfg.writeEntry(QStringLiteral("defaultEmailClient"), m_defaultEmailClient);
    cfg.writeEntry(QStringLiteral("defaultCalendarClient"), m_defaultCalendarClient);

    cfg.sync();

    notifyDaemon();
    setNeedsSave(false);
}

void KcmKity::defaults()
{
    setWakeWordEnabled(true);
    setSpeechOutputMode(QStringLiteral("always"));
    setEarconsEnabled(true);
    setVadEnabled(true);
    setVadSilenceTimeoutMs(1300);
    setAutoListenOnActivate(false);
    setWhisperLanguage(QStringLiteral("es"));
    setWhisperModelPath(QString());
    setPiperVoicePath(QString());

    setOllamaUrl(QStringLiteral("http://127.0.0.1:11434"));
    setOllamaModel(QStringLiteral("llama3.2:3b"));
    setOllamaVisionModel(QStringLiteral("llama3.2:3b"));
    setOllamaTemperature(0.2);
    setOllamaNumCtx(4096);
    setOllamaNumThread(6);
    setOllamaNumPredict(384);
    setOllamaKeepAlive(QStringLiteral("24h"));
    setSystemPrompt(defaultSystemPrompt());

    setKnowledgeBaseEnabled(true);
    setKnowledgeBaseMaxSnippets(3);

    setPanelIconStyle(QStringLiteral("symbolic"));
    setPin(false);
    setDefaultEmailClient(QStringLiteral("proton"));
    setDefaultCalendarClient(QStringLiteral("proton"));

    setNeedsSave(true);
}

void KcmKity::setWakeWordEnabled(bool v)
{
    if (m_wakeWordEnabled != v) {
        m_wakeWordEnabled = v;
        Q_EMIT wakeWordEnabledChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setSpeechOutputMode(const QString &v)
{
    if (m_speechOutputMode != v) {
        m_speechOutputMode = v;
        Q_EMIT speechOutputModeChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setEarconsEnabled(bool v)
{
    if (m_earconsEnabled != v) {
        m_earconsEnabled = v;
        Q_EMIT earconsEnabledChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setVadEnabled(bool v)
{
    if (m_vadEnabled != v) {
        m_vadEnabled = v;
        Q_EMIT vadEnabledChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setVadSilenceTimeoutMs(int v)
{
    if (m_vadSilenceTimeoutMs != v) {
        m_vadSilenceTimeoutMs = v;
        Q_EMIT vadSilenceTimeoutMsChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setAutoListenOnActivate(bool v)
{
    if (m_autoListenOnActivate != v) {
        m_autoListenOnActivate = v;
        Q_EMIT autoListenOnActivateChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setWhisperLanguage(const QString &v)
{
    if (m_whisperLanguage != v) {
        m_whisperLanguage = v;
        Q_EMIT whisperLanguageChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setWhisperModelPath(const QString &v)
{
    if (m_whisperModelPath != v) {
        m_whisperModelPath = v;
        Q_EMIT whisperModelPathChanged();
        Q_EMIT diagnosticsChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setPiperVoicePath(const QString &v)
{
    if (m_piperVoicePath != v) {
        m_piperVoicePath = v;
        Q_EMIT piperVoicePathChanged();
        Q_EMIT diagnosticsChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setOllamaUrl(const QString &v)
{
    if (m_ollamaUrl != v) {
        m_ollamaUrl = v;
        Q_EMIT ollamaUrlChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setOllamaModel(const QString &v)
{
    if (m_ollamaModel != v) {
        m_ollamaModel = v;
        Q_EMIT ollamaModelChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setOllamaVisionModel(const QString &v)
{
    if (m_ollamaVisionModel != v) {
        m_ollamaVisionModel = v;
        Q_EMIT ollamaVisionModelChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setOllamaTemperature(double v)
{
    if (!qFuzzyCompare(m_ollamaTemperature, v)) {
        m_ollamaTemperature = v;
        Q_EMIT ollamaTemperatureChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setOllamaNumCtx(int v)
{
    if (m_ollamaNumCtx != v) {
        m_ollamaNumCtx = v;
        Q_EMIT ollamaNumCtxChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setOllamaNumThread(int v)
{
    if (m_ollamaNumThread != v) {
        m_ollamaNumThread = v;
        Q_EMIT ollamaNumThreadChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setOllamaNumPredict(int v)
{
    if (m_ollamaNumPredict != v) {
        m_ollamaNumPredict = v;
        Q_EMIT ollamaNumPredictChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setOllamaKeepAlive(const QString &v)
{
    if (m_ollamaKeepAlive != v) {
        m_ollamaKeepAlive = v;
        Q_EMIT ollamaKeepAliveChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setSystemPrompt(const QString &v)
{
    if (m_systemPrompt != v) {
        m_systemPrompt = v;
        Q_EMIT systemPromptChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setKnowledgeBaseEnabled(bool v)
{
    if (m_knowledgeBaseEnabled != v) {
        m_knowledgeBaseEnabled = v;
        Q_EMIT knowledgeBaseEnabledChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setKnowledgeBaseMaxSnippets(int v)
{
    if (m_knowledgeBaseMaxSnippets != v) {
        m_knowledgeBaseMaxSnippets = v;
        Q_EMIT knowledgeBaseMaxSnippetsChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setPanelIconStyle(const QString &v)
{
    if (m_panelIconStyle != v) {
        m_panelIconStyle = v;
        Q_EMIT panelIconStyleChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setPin(bool v)
{
    if (m_pin != v) {
        m_pin = v;
        Q_EMIT pinChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setDefaultEmailClient(const QString &v)
{
    if (m_defaultEmailClient != v) {
        m_defaultEmailClient = v;
        Q_EMIT defaultEmailClientChanged();
        setNeedsSave(true);
    }
}

void KcmKity::setDefaultCalendarClient(const QString &v)
{
    if (m_defaultCalendarClient != v) {
        m_defaultCalendarClient = v;
        Q_EMIT defaultCalendarClientChanged();
        setNeedsSave(true);
    }
}

bool KcmKity::whisperFound() const
{
    if (!m_whisperModelPath.isEmpty() && QFile::exists(m_whisperModelPath)) {
        return true;
    }
    const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity/models");
    QDir dir(defaultDir);
    return !dir.entryList(QStringList() << QStringLiteral("*.bin"), QDir::Files).isEmpty();
}

bool KcmKity::piperFound() const
{
    if (!m_piperVoicePath.isEmpty() && QFile::exists(m_piperVoicePath)) {
        return true;
    }
    const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity/voices");
    QDir dir(defaultDir);
    return !dir.entryList(QStringList() << QStringLiteral("*.onnx"), QDir::Files).isEmpty();
}

void KcmKity::fetchModels()
{
    const QString baseUrl = m_ollamaUrl.trimmed().isEmpty() ? QStringLiteral("http://127.0.0.1:11434") : m_ollamaUrl.trimmed();
    QUrl url(baseUrl + QStringLiteral("/api/tags"));
    QNetworkRequest request(url);
    request.setTransferTimeout(4000);

    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray data = reply->readAll();
            const QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isObject()) {
                const QJsonArray arr = doc.object().value(QStringLiteral("models")).toArray();
                QStringList list;
                for (const QJsonValue &v : arr) {
                    const QString name = v.toObject().value(QStringLiteral("name")).toString();
                    if (!name.isEmpty()) {
                        list.append(name);
                    }
                }
                if (!list.isEmpty()) {
                    if (!m_ollamaModel.isEmpty() && !list.contains(m_ollamaModel)) {
                        list.prepend(m_ollamaModel);
                    }
                    m_detectedModels = list;
                    Q_EMIT detectedModelsChanged();
                    Q_EMIT diagnosticsChanged();
                }
            }
        }
    });
}

void KcmKity::checkReadiness()
{
    m_ollamaChecking = true;
    Q_EMIT ollamaCheckingChanged();

    const QString baseUrl = m_ollamaUrl.trimmed().isEmpty() ? QStringLiteral("http://127.0.0.1:11434") : m_ollamaUrl.trimmed();
    QUrl url(baseUrl + QStringLiteral("/api/version"));
    QNetworkRequest request(url);
    request.setTransferTimeout(3000);

    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_ollamaChecking = false;
        m_ollamaReachable = (reply->error() == QNetworkReply::NoError);
        Q_EMIT ollamaCheckingChanged();
        Q_EMIT ollamaReachableChanged();
        Q_EMIT diagnosticsChanged();
    });
}

void KcmKity::openKnowledgeFolder() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity/knowledge");
    QDir().mkpath(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void KcmKity::restoreDefaultPrompt()
{
    setSystemPrompt(defaultSystemPrompt());
}

void KcmKity::notifyDaemon()
{
    QDBusInterface iface(QStringLiteral("org.cesar.kity"),
                         QStringLiteral("/Kity"),
                         QStringLiteral("org.cesar.kity"),
                         QDBusConnection::sessionBus());
    if (iface.isValid()) {
        iface.call(QDBus::NoBlock, QStringLiteral("reloadConfig"));
    }
}

#include "kcm_kity.moc"
