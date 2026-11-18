/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "llmengine.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>
#include <KConfigGroup>
#include <KLocalizedContext>
#include <KSharedConfig>

#include "../system/hardwaremanager.h"
#include "../system/emailmanager.h"
#include "../system/calendarmanager.h"
#include "../tools/toolregistry.h"

namespace Kity {

namespace {

QString readConfigString(const QString &key, const QString &def = QString())
{
    const KSharedConfig::Ptr kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    const KConfigGroup cfg = kityrc->group(QStringLiteral("General"));
    return cfg.readEntry(key, def);
}

int readConfigInt(const QString &key, int def = 0)
{
    const KSharedConfig::Ptr kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    const KConfigGroup cfg = kityrc->group(QStringLiteral("General"));
    return cfg.readEntry(key, def);
}

double readConfigDouble(const QString &key, double def = 0.0)
{
    const KSharedConfig::Ptr kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    const KConfigGroup cfg = kityrc->group(QStringLiteral("General"));
    return cfg.readEntry(key, def);
}

} // anonymous namespace

LlmEngine &LlmEngine::instance()
{
    static LlmEngine s_instance;
    return s_instance;
}

LlmEngine::LlmEngine(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    refreshModels();
}

LlmEngine::~LlmEngine()
{
    stop();
}

QString LlmEngine::resolveOllamaUrl()
{
    const QString url = readConfigString(QStringLiteral("ollamaUrl"), QStringLiteral("http://127.0.0.1:11434"));
    return url.endsWith(QLatin1Char('/')) ? url.left(url.length() - 1) : url;
}

QString LlmEngine::resolveOllamaModel()
{
    return readConfigString(QStringLiteral("ollamaModel"), QStringLiteral("llama3.2:3b"));
}

QString LlmEngine::resolveVisionModel()
{
    return readConfigString(QStringLiteral("visionModel"), QStringLiteral("llama3.2-vision:11b"));
}

QString LlmEngine::detectAvailableVisionModel()
{
    const QString custom = resolveVisionModel();
    if (!custom.isEmpty() && custom != QStringLiteral("auto")) {
        return custom;
    }
    const auto &models = instance().m_availableModels;
    for (const QString &m : models) {
        if (m.contains(QStringLiteral("vision"), Qt::CaseInsensitive)) {
            return m;
        }
    }
    for (const QString &m : models) {
        if (m.contains(QStringLiteral("moondream"), Qt::CaseInsensitive) ||
            m.contains(QStringLiteral("llava"), Qt::CaseInsensitive) ||
            m.contains(QStringLiteral("bakllava"), Qt::CaseInsensitive)) {
            return m;
        }
    }
    return QStringLiteral("llama3.2-vision:11b");
}

QString LlmEngine::resolveGeminiApiKey()
{
    return readConfigString(QStringLiteral("geminiApiKey")).trimmed();
}

QString LlmEngine::resolveGeminiModel()
{
    return readConfigString(QStringLiteral("geminiModel"), QStringLiteral("gemini-1.5-flash"));
}

QString LlmEngine::resolveSystemPrompt()
{
    const QString custom = readConfigString(QStringLiteral("customSystemPrompt"));
    if (!custom.isEmpty()) {
        return custom;
    }
    return QStringLiteral(
        "Eres Kity, el asistente de voz e inteligencia artificial nativo para CachyOS Linux con KDE Plasma 6 bajo Wayland. "
        "Tu diseño combina la elegancia y calidez de Siri y Apple Intelligence con la precisión y potencia de Google Gemini. "
        "Responde de forma concisa, útil, empática y en un tono completamente natural. "
        "Tus respuestas deben ser redactadas exclusivamente en español de España (castellano), con gramática y ortografía impecables. "
        "Cuando el usuario te pregunte cosas cotidianas, responde con claridad ejecutiva y directa.");
}

QString LlmEngine::streamingText() const
{
    return m_streamingText;
}

QStringList LlmEngine::availableModels() const
{
    return m_availableModels;
}

bool LlmEngine::isOllamaReachable() const
{
    return m_ollamaReachable;
}

void LlmEngine::setPendingCard(const QString &cardType, const QVariantMap &cardData)
{
    m_pendingCardType = cardType;
    m_pendingCardData = cardData;
}

void LlmEngine::setWritingToolContext(bool isWritingTool, const QString &sourceText, const QString &action)
{
    m_lastWasWritingTool = isWritingTool;
    m_lastWritingToolSource = sourceText;
    m_lastWritingToolAction = action;
}

void LlmEngine::stop()
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
    m_streamingText.clear();
    Q_EMIT streamingTextChanged(m_streamingText);
}

void LlmEngine::refreshModels()
{
    const QUrl tagsUrl(resolveOllamaUrl() + QStringLiteral("/api/tags"));
    QNetworkRequest req(tagsUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Kity/1.0"));

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const bool reachable = (reply->error() == QNetworkReply::NoError);
        if (m_ollamaReachable != reachable) {
            m_ollamaReachable = reachable;
            Q_EMIT ollamaReachabilityChanged(reachable);
        }

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
                Q_EMIT availableModelsChanged(m_availableModels);
            }
        }
    });
}

void LlmEngine::query(const QString &userPrompt,
                      const QString &injectedContext,
                      const QString &base64Image,
                      const QVariantList &historyMessages,
                      bool forceGemini)
{
    stop();

    m_lastOllamaPrompt = userPrompt;
    m_lastOllamaContext = injectedContext;
    m_lastOllamaImage = base64Image;
    m_lastHistoryMessages = historyMessages;

    const QString aiBackend = readConfigString(QStringLiteral("aiBackend"), QStringLiteral("hybrid")).toLower();
    bool useGemini = forceGemini;

    if (!forceGemini) {
        const QString geminiApiKey = resolveGeminiApiKey();
        if (aiBackend == QLatin1String("gemini")) {
            useGemini = !geminiApiKey.isEmpty();
        } else if (aiBackend == QLatin1String("hybrid")) {
            static const QRegularExpression localActionSignal(
                QStringLiteral("^(?:sube|baja|ajusta|pon|silencia|abre|abrir|ejecuta|cierra|temporizador|alarma|bater[ií]a|brillo|volumen)"),
                QRegularExpression::CaseInsensitiveOption);
            const bool isLocalAction = userPrompt.contains(localActionSignal);
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
    }

    if (useGemini) {
        sendToGemini(userPrompt, injectedContext, base64Image, historyMessages);
    } else {
        sendToOllama(userPrompt, injectedContext, base64Image, historyMessages);
    }
}

void LlmEngine::sendToOllama(const QString &userPrompt,
                             const QString &injectedContext,
                             const QString &base64Image,
                             const QVariantList &historyMessages)
{
    Q_EMIT stateChanged(QStringLiteral("thinking"), QStringLiteral("Generando respuesta…"));
    m_streamingText.clear();
    Q_EMIT streamingTextChanged(m_streamingText);

    const bool hasVision = !base64Image.isEmpty();
    QString modelToUse = hasVision ? resolveVisionModel() : resolveOllamaModel();
    if (modelToUse.isEmpty() || modelToUse.contains(QStringLiteral("moondream"), Qt::CaseInsensitive)) {
        modelToUse = resolveOllamaModel();
    }

    QJsonArray chatMsgs;

    // 1. System prompt
    QJsonObject sysObj;
    sysObj[QStringLiteral("role")] = QStringLiteral("system");
    QString fullSysPrompt = resolveSystemPrompt();
    if (!fullSysPrompt.contains(QStringLiteral("español"), Qt::CaseInsensitive)) {
        fullSysPrompt.prepend(QStringLiteral("REGLA ESTRICTA DE IDIOMA: Responde SIEMPRE y exclusivamente en español de España (castellano).\n\n"));
    }
    const QString currentDateStr = QLocale(QLocale::Spanish, QLocale::Spain).toString(QDateTime::currentDateTime(), QStringLiteral("dddd, d 'de' MMMM 'de' yyyy, HH:mm"));
    fullSysPrompt.append(QStringLiteral("\n[Fecha y hora actual del sistema: %1]").arg(currentDateStr));
    fullSysPrompt.append(QStringLiteral(
        "\n[IDENTIDAD Y CONOCIMIENTO ESTRICTO DEL SISTEMA OPERATIVO:]\n"
        "- Eres Kity, el asistente de voz e inteligencia artificial nativo para CachyOS Linux con KDE Plasma 6 bajo Wayland.\n"
        "- Sistema Operativo: CachyOS Linux (distribución rolling-release de alto rendimiento basada en Arch Linux).\n"
        "- Gestor de actualizaciones oficial: 'cachy-update'. Gestores de paquetes: 'pacman' y 'paru' (AUR).\n"
        "- Terminal predeterminada del sistema: Konsole.\n"
        "- REGLA ESTRICTA DE LINUX: Este sistema NUNCA es Debian ni Ubuntu. NUNCA menciones ni uses 'apt', 'apt-get', 'dnf', 'yum' ni 'zypper'. Para cualquier tarea o recomendación de comandos de terminal, usa SIEMPRE comandos de Arch / CachyOS (cachy-update, pacman, paru, systemctl, etc.). Si el usuario pide actualizar el sistema, ejecutar comandos o abrir la terminal, invoca la herramienta 'run_terminal_command' con el comando 'cachy-update' o recomienda 'cachy-update'. La terminal Konsole se cerrará automáticamente al finalizar la actualización si el proceso concluye con éxito.\n"));
    fullSysPrompt.append(QStringLiteral(
        "\n[Acceso a Internet en tiempo real:]\n"
        "Dispones de acceso directo a Internet mediante búsqueda web ('web_search') y mediante el contexto web provisto en tiempo real. "
        "Cuando el usuario pregunte por fechas, noticias, acontecimientos actuales o futuros (año 2026 en adelante), "
        "resultados electorales o deportivos, personas o cualquier dato de actualidad, "
        "basa tu respuesta en la información de internet provista o invoca la herramienta 'web_search'. "
        "NUNCA digas que no tienes información actualizada sin antes consultar la búsqueda web."));

    const QString vitals = HardwareManager::instance().collectSystemVitals();
    if (!vitals.isEmpty()) {
        fullSysPrompt.append(QStringLiteral("\n[System Environment: %1]").arg(vitals));
    }
    if (!injectedContext.isEmpty()) {
        fullSysPrompt.append(QStringLiteral("\n[Additional Relevant Context:\n%1]").arg(injectedContext.trimmed()));
    }
    sysObj[QStringLiteral("content")] = fullSysPrompt;
    chatMsgs.append(sysObj);

    // 2. Recent history (last 8 messages)
    const int startIdx = qMax(0, historyMessages.size() - 8);
    for (int i = startIdx; i < historyMessages.size() - 1; ++i) {
        const QVariantMap vm = historyMessages.at(i).toMap();
        const QString content = vm.value(QStringLiteral("content")).toString();
        if (content.contains(QStringLiteral("necesitaría que me describieras"), Qt::CaseInsensitive)) {
            continue;
        }
        QJsonObject mObj;
        mObj[QStringLiteral("role")] = vm.value(QStringLiteral("role")).toString();
        mObj[QStringLiteral("content")] = content;
        chatMsgs.append(mObj);
    }

    // 3. Current user message
    QJsonObject userObj;
    userObj[QStringLiteral("role")] = QStringLiteral("user");
    userObj[QStringLiteral("content")] = userPrompt;
    if (hasVision) {
        QJsonArray imgs;
        imgs.append(base64Image);
        userObj[QStringLiteral("images")] = imgs;
    }
    chatMsgs.append(userObj);

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
    const QString keepAlive = readConfigString(QStringLiteral("ollamaKeepAlive"), QStringLiteral("15m"));
    reqPayload[QStringLiteral("keep_alive")] = keepAlive.isEmpty() ? QStringLiteral("15m") : keepAlive;

    if (!hasVision) {
        const QString modelLower = modelToUse.toLower();
        const bool isLlama32 = modelLower.contains(QStringLiteral("llama3.2"));
        const QJsonArray tools = ToolRegistry::instance().getToolDefinitions(isLlama32, userPrompt);
        if (!tools.isEmpty()) {
            reqPayload[QStringLiteral("tools")] = tools;
        }
    }

    const QUrl chatUrl(resolveOllamaUrl() + QStringLiteral("/api/chat"));
    QNetworkRequest req(chatUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const int timeoutMs = readConfigInt(QStringLiteral("ollamaTimeoutMs"), 20000);
    req.setTransferTimeout(timeoutMs);

    if (m_currentOllamaReply) {
        m_currentOllamaReply->abort();
        m_currentOllamaReply->deleteLater();
    }

    m_currentOllamaReply = m_nam->post(req, QJsonDocument(reqPayload).toJson(QJsonDocument::Compact));
    connect(m_currentOllamaReply, &QNetworkReply::readyRead, this, &LlmEngine::handleOllamaStreamChunk);
    connect(m_currentOllamaReply, &QNetworkReply::finished, this, &LlmEngine::handleOllamaFinished);
}

void LlmEngine::handleOllamaStreamChunk()
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
            Q_EMIT errorOccurred(QStringLiteral("Error de Ollama: %1").arg(err));
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
            Q_EMIT streamingTextChanged(m_streamingText);
            Q_EMIT streamingChunkReceived(chunk);
        }
    }
}

void LlmEngine::handleOllamaFinished()
{
    if (!m_currentOllamaReply) return;

    const bool error = (m_currentOllamaReply->error() != QNetworkReply::NoError &&
                        m_currentOllamaReply->error() != QNetworkReply::OperationCanceledError);

    const QNetworkReply::NetworkError errCode = m_currentOllamaReply->error();
    const QString httpError = m_currentOllamaReply->errorString();
    m_currentOllamaReply->deleteLater();
    m_currentOllamaReply = nullptr;

    if (error) {
        const QString geminiKey = resolveGeminiApiKey();
        if (!geminiKey.isEmpty() && !m_lastOllamaPrompt.isEmpty() && m_streamingText.isEmpty()) {
            qWarning() << "Kity: Ollama unreachable or timed out (" << httpError << "). Fallback to Google Gemini.";
            Q_EMIT stateChanged(QStringLiteral("thinking"), QStringLiteral("Ollama no responde. Consultando con Google Gemini…"));
            sendToGemini(m_lastOllamaPrompt, m_lastOllamaContext, m_lastOllamaImage, m_lastHistoryMessages);
            return;
        }

        QString friendlyErr;
        if (errCode == QNetworkReply::ConnectionRefusedError || errCode == QNetworkReply::HostNotFoundError) {
            friendlyErr = QStringLiteral("Ollama no está en ejecución. Inícialo con 'ollama serve' o revisa la URL.");
        } else if (errCode == QNetworkReply::TimeoutError) {
            friendlyErr = QStringLiteral("Tiempo de espera agotado al consultar a Ollama.");
        } else {
            friendlyErr = QStringLiteral("Error al comunicarse con Ollama: %1").arg(httpError);
        }
        Q_EMIT errorOccurred(friendlyErr);
        return;
    }

    if (!m_pendingToolCalls.isEmpty()) {
        QJsonObject asstMsg;
        asstMsg[QStringLiteral("role")] = QStringLiteral("assistant");
        asstMsg[QStringLiteral("content")] = QStringLiteral("");
        asstMsg[QStringLiteral("tool_calls")] = m_pendingToolCalls;
        m_currentChatMessages.append(asstMsg);

        for (const QJsonValue &tcVal : m_pendingToolCalls) {
            const QJsonObject tcObj = tcVal.toObject();
            const QString callId = tcObj.value(QStringLiteral("id")).toString();
            const QJsonObject funcObj = tcObj.value(QStringLiteral("function")).toObject();
            const QString toolName = funcObj.value(QStringLiteral("name")).toString();
            const QJsonObject toolArgs = funcObj.value(QStringLiteral("arguments")).toObject();

            Q_EMIT stateChanged(QStringLiteral("thinking"), QStringLiteral("Ejecutando acción del sistema: %1…").arg(toolName));

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
    static const QRegularExpression thinkRegex(QStringLiteral("<think>.*?</think>"), QRegularExpression::DotMatchesEverythingOption);
    finalResponse.remove(thinkRegex);
    finalResponse = finalResponse.trimmed();

    LlmResponse resp;
    resp.text = finalResponse;
    resp.isWritingTool = m_lastWasWritingTool;
    resp.sourceText = m_lastWritingToolSource;
    resp.writingToolAction = m_lastWritingToolAction;
    resp.cardType = m_pendingCardType;
    resp.cardData = m_pendingCardData;

    m_pendingCardType.clear();
    m_pendingCardData.clear();
    m_lastWasWritingTool = false;

    // Extract Markdown links as structured sources
    static const QRegularExpression mdLinkRx(QStringLiteral("\\[([^\\]]+)\\]\\((https?://[^\\)]+)\\)"));
    auto linkIt = mdLinkRx.globalMatch(finalResponse);
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
            resp.sources.append(s);
        }
    }

    m_streamingText.clear();
    Q_EMIT streamingTextChanged(m_streamingText);
    Q_EMIT responseFinished(resp);
}

void LlmEngine::sendOllamaToolFollowUp(const QJsonArray &messages)
{
    Q_EMIT stateChanged(QStringLiteral("thinking"), QStringLiteral("Generando respuesta…"));
    m_streamingText.clear();
    Q_EMIT streamingTextChanged(m_streamingText);

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
    const QString keepAlive = readConfigString(QStringLiteral("ollamaKeepAlive"), QStringLiteral("15m"));
    reqPayload[QStringLiteral("keep_alive")] = keepAlive.isEmpty() ? QStringLiteral("15m") : keepAlive;

    const QUrl chatUrl(resolveOllamaUrl() + QStringLiteral("/api/chat"));
    QNetworkRequest req(chatUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(readConfigInt(QStringLiteral("ollamaTimeoutMs"), 20000));

    if (m_currentOllamaReply) {
        m_currentOllamaReply->abort();
        m_currentOllamaReply->deleteLater();
    }

    m_currentOllamaReply = m_nam->post(req, QJsonDocument(reqPayload).toJson(QJsonDocument::Compact));
    connect(m_currentOllamaReply, &QNetworkReply::readyRead, this, &LlmEngine::handleOllamaStreamChunk);
    connect(m_currentOllamaReply, &QNetworkReply::finished, this, &LlmEngine::handleOllamaFinished);
}

void LlmEngine::sendToGemini(const QString &userPrompt,
                             const QString &injectedContext,
                             const QString &base64Image,
                             const QVariantList &historyMessages)
{
    Q_EMIT stateChanged(QStringLiteral("thinking"), QStringLiteral("Consultando con Google Gemini…"));
    m_streamingText.clear();
    m_geminiSources.clear();
    Q_EMIT streamingTextChanged(m_streamingText);

    const QString apiKey = resolveGeminiApiKey();
    if (apiKey.isEmpty()) {
        Q_EMIT errorOccurred(QStringLiteral("Clave API de Google Gemini no configurada. Configúrala en los Ajustes de Kity."));
        return;
    }

    const QString geminiModel = resolveGeminiModel();
    const QString modelToUse = (geminiModel.isEmpty() ? QStringLiteral("gemini-1.5-flash") : geminiModel);
    const bool hasVision = !base64Image.isEmpty();
    m_lastGeminiPrompt = userPrompt;

    // 1. System instruction
    QString fullSysPrompt = resolveSystemPrompt();
    if (!fullSysPrompt.contains(QStringLiteral("español"), Qt::CaseInsensitive)) {
        fullSysPrompt.prepend(QStringLiteral("REGLA ESTRICTA DE IDIOMA: Responde SIEMPRE y exclusivamente en español de España (castellano).\n\n"));
    }
    const QString currentDateStr = QLocale(QLocale::Spanish, QLocale::Spain).toString(QDateTime::currentDateTime(), QStringLiteral("dddd, d 'de' MMMM 'de' yyyy, HH:mm"));
    fullSysPrompt.append(QStringLiteral("\n[Fecha y hora actual del sistema: %1]").arg(currentDateStr));
    fullSysPrompt.append(QStringLiteral(
        "\n[IDENTIDAD Y CONOCIMIENTO ESTRICTO DEL SISTEMA OPERATIVO:]\n"
        "- Eres Kity, el asistente de voz e inteligencia artificial nativo para CachyOS Linux con KDE Plasma 6 bajo Wayland.\n"
        "- Sistema Operativo: CachyOS Linux (distribución rolling-release de alto rendimiento basada en Arch Linux).\n"
        "- Gestor de actualizaciones oficial: 'cachy-update'. Gestores de paquetes: 'pacman' y 'paru' (AUR).\n"
        "- Terminal predeterminada del sistema: Konsole.\n"
        "- REGLA ESTRICTA DE LINUX: Este sistema NUNCA es Debian ni Ubuntu. NUNCA menciones ni uses 'apt', 'apt-get', 'dnf', 'yum' ni 'zypper'. Para cualquier tarea o recomendación de comandos de terminal, usa SIEMPRE comandos de Arch / CachyOS (cachy-update, pacman, paru, systemctl, etc.). Si el usuario pide actualizar el sistema, ejecutar comandos o abrir la terminal, invoca la herramienta 'run_terminal_command' con el comando 'cachy-update' o recomienda 'cachy-update'. La terminal Konsole se cerrará automáticamente al finalizar la actualización si el proceso concluye con éxito.\n"));

    const QString vitals = HardwareManager::instance().collectSystemVitals();
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
    const int startIdx = qMax(0, historyMessages.size() - 8);
    for (int i = startIdx; i < historyMessages.size() - 1; ++i) {
        const QVariantMap vm = historyMessages.at(i).toMap();
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
        inlineData[QStringLiteral("mime_type")] = QStringLiteral("image/jpeg");
        inlineData[QStringLiteral("data")] = base64Image;
        QJsonObject imgPart;
        imgPart[QStringLiteral("inline_data")] = inlineData;
        currentParts.append(imgPart);
    }
    currentTurn[QStringLiteral("parts")] = currentParts;
    contents.append(currentTurn);

    QJsonObject payload;
    payload[QStringLiteral("system_instruction")] = sysInstruction;
    payload[QStringLiteral("contents")] = contents;

    QJsonObject genConfig;
    genConfig[QStringLiteral("temperature")] = readConfigDouble(QStringLiteral("ollamaTemperature"), 0.2);
    genConfig[QStringLiteral("maxOutputTokens")] = 2048;
    payload[QStringLiteral("generationConfig")] = genConfig;

    // Google Search Grounding
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
    connect(m_currentGeminiReply, &QNetworkReply::readyRead, this, &LlmEngine::handleGeminiStreamChunk);
    connect(m_currentGeminiReply, &QNetworkReply::finished, this, &LlmEngine::handleGeminiFinished);
}

void LlmEngine::handleGeminiStreamChunk()
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
            qWarning() << "Gemini stream error:" << errMsg;
            if (m_ollamaReachable || readConfigString(QStringLiteral("aiBackend"), QStringLiteral("hybrid")).toLower() == QLatin1String("hybrid")) {
                if (m_currentGeminiReply) {
                    m_currentGeminiReply->abort();
                    m_currentGeminiReply->deleteLater();
                    m_currentGeminiReply = nullptr;
                }
                sendToOllama(m_lastGeminiPrompt, m_lastOllamaContext, m_lastOllamaImage, m_lastHistoryMessages);
                return;
            }
            Q_EMIT errorOccurred(QStringLiteral("Error de Google Gemini: %1").arg(errMsg));
            return;
        }

        const QJsonArray candidates = obj.value(QStringLiteral("candidates")).toArray();
        if (candidates.isEmpty()) continue;

        const QJsonObject candidate = candidates.first().toObject();

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
                Q_EMIT streamingTextChanged(m_streamingText);
                Q_EMIT streamingChunkReceived(textChunk);
            }
        }
    }
}

void LlmEngine::handleGeminiFinished()
{
    if (!m_currentGeminiReply) return;

    const int statusCode = m_currentGeminiReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool error = (statusCode >= 400 || (m_currentGeminiReply->error() != QNetworkReply::NoError &&
                        m_currentGeminiReply->error() != QNetworkReply::OperationCanceledError));

    const QString httpError = m_currentGeminiReply->errorString();
    m_currentGeminiReply->deleteLater();
    m_currentGeminiReply = nullptr;

    if (error || m_streamingText.trimmed().isEmpty()) {
        qWarning() << "Kity: Gemini failed or empty -> Falling back to Ollama local";
        if (m_ollamaReachable || readConfigString(QStringLiteral("aiBackend"), QStringLiteral("hybrid")).toLower() == QLatin1String("hybrid")) {
            sendToOllama(m_lastGeminiPrompt, m_lastOllamaContext, m_lastOllamaImage, m_lastHistoryMessages);
            return;
        }
        Q_EMIT errorOccurred(QStringLiteral("Error al comunicarse con Google Gemini: %1").arg(httpError));
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

    LlmResponse resp;
    resp.text = finalResponse;
    resp.isWritingTool = m_lastWasWritingTool;
    resp.sourceText = m_lastWritingToolSource;
    resp.writingToolAction = m_lastWritingToolAction;
    resp.cardType = m_pendingCardType;
    resp.cardData = m_pendingCardData;

    m_pendingCardType.clear();
    m_pendingCardData.clear();
    m_lastWasWritingTool = false;

    for (const auto &pair : m_geminiSources) {
        QVariantMap s;
        s[QStringLiteral("title")] = pair.first;
        s[QStringLiteral("url")] = pair.second;
        resp.sources.append(s);
    }

    m_streamingText.clear();
    Q_EMIT streamingTextChanged(m_streamingText);
    Q_EMIT responseFinished(resp);
}

QJsonObject LlmEngine::executeToolCall(const QString &toolName, const QJsonObject &arguments)
{
    const QJsonObject res = ToolRegistry::instance().executeTool(toolName, arguments);
    if (toolName == QLatin1String("email_tool")) {
        if (res.contains(QStringLiteral("draft"))) {
            m_pendingCardType = QStringLiteral("email");
            const EmailDraft draft = EmailManager::instance().currentDraft();
            m_pendingCardData = draft.toCardMap();
        }
    } else if (toolName == QLatin1String("calendar_event")) {
        if (res.value(QStringLiteral("success")).toBool()) {
            m_pendingCardType = QStringLiteral("event");
            const CalendarEvent ce = CalendarManager::instance().parsePrompt(arguments.value(QStringLiteral("prompt")).toString());
            m_pendingCardData = ce.toCardMap();
        }
    }
    return res;
}

QString LlmEngine::analyzeImageWithVisionModel(const QString &base64Image)
{
    if (base64Image.isEmpty()) {
        return QString();
    }

    QJsonObject reqPayload;
    reqPayload[QStringLiteral("model")] = detectAvailableVisionModel();
    reqPayload[QStringLiteral("prompt")] = QStringLiteral("Describe accurately and in detail everything visible on this computer screen, including open applications, window titles, visible text, and interface elements.");
    reqPayload[QStringLiteral("stream")] = false;
    reqPayload[QStringLiteral("keep_alive")] = QStringLiteral("15m");

    QJsonObject opts;
    opts[QStringLiteral("temperature")] = 0.2;
    reqPayload[QStringLiteral("options")] = opts;

    QJsonArray imgs;
    imgs.append(base64Image);
    reqPayload[QStringLiteral("images")] = imgs;

    const QUrl genUrl(resolveOllamaUrl() + QStringLiteral("/api/generate"));
    QNetworkRequest req(genUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkAccessManager mgr;
    QNetworkReply *reply = mgr.post(req, QJsonDocument(reqPayload).toJson(QJsonDocument::Compact));
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(25000);
    loop.exec();

    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        const QByteArray rawResp = reply->readAll();
        reply->deleteLater();
        const QJsonDocument doc = QJsonDocument::fromJson(rawResp);
        return doc.object().value(QStringLiteral("response")).toString().trimmed();
    }
    reply->abort();
    reply->deleteLater();
    return QString();
}

} // namespace Kity
