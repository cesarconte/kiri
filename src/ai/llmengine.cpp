/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "llmengine.h"
#include "../system/configmanager.h"
#include "../system/hardwaremanager.h"
#include "../system/emailmanager.h"
#include "../system/calendarmanager.h"
#include "../tools/toolregistry.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <QUrl>

#include <KConfigGroup>
#include <KSharedConfig>

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
    , m_ollamaClient(std::make_unique<OllamaClient>(m_nam, this))
    , m_geminiClient(std::make_unique<GeminiClient>(m_nam, this))
{
    connect(m_ollamaClient.get(), &OllamaClient::chunkReceived, this, [this](const QString &chunk) {
        Q_EMIT streamingChunkReceived(chunk);
    });
    connect(m_ollamaClient.get(), &OllamaClient::streamingTextChanged, this, &LlmEngine::streamingTextChanged);
    connect(m_ollamaClient.get(), &OllamaClient::toolCallsDetected, this, &LlmEngine::handleOllamaToolCalls);
    connect(m_ollamaClient.get(), &OllamaClient::finished, this, [this](const QString &rawResponse) {
        QString finalResponse = rawResponse.trimmed();
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
        Q_EMIT responseFinished(resp);
    });
    connect(m_ollamaClient.get(), &OllamaClient::errorOccurred, this, [this](const QString &errMsg, bool fallbackEligible) {
        const QString geminiKey = resolveGeminiApiKey();
        if (fallbackEligible && !geminiKey.isEmpty() && !m_lastOllamaPrompt.isEmpty()) {
            qWarning() << "Kity: Ollama unreachable -> Fallback to Google Gemini.";
            Q_EMIT stateChanged(QStringLiteral("thinking"), QStringLiteral("Ollama no responde. Consultando con Google Gemini…"));
            sendToGemini(m_lastOllamaPrompt, m_lastOllamaContext, m_lastOllamaImage, m_lastHistoryMessages);
            return;
        }
        Q_EMIT errorOccurred(errMsg);
    });
    connect(m_ollamaClient.get(), &OllamaClient::modelsRefreshed, this, [this](const QStringList &models, bool reachable) {
        bool changed = false;
        if (m_availableModels != models) {
            m_availableModels = models;
            changed = true;
        }
        if (m_ollamaReachable != reachable) {
            m_ollamaReachable = reachable;
            Q_EMIT ollamaReachabilityChanged(reachable);
        }
        if (changed) {
            Q_EMIT availableModelsChanged(m_availableModels);
        }
    });

    connect(m_geminiClient.get(), &GeminiClient::chunkReceived, this, [this](const QString &chunk) {
        Q_EMIT streamingChunkReceived(chunk);
    });
    connect(m_geminiClient.get(), &GeminiClient::streamingTextChanged, this, &LlmEngine::streamingTextChanged);
    connect(m_geminiClient.get(), &GeminiClient::finished, this, [this](const QString &rawResponse, const QList<QPair<QString, QString>> &sources) {
        QString finalResponse = rawResponse.trimmed();
        static const QRegularExpression thinkRegex(QStringLiteral("<think>.*?</think>"), QRegularExpression::DotMatchesEverythingOption);
        finalResponse.remove(thinkRegex);
        finalResponse = finalResponse.trimmed();

        if (!sources.isEmpty()) {
            static const QRegularExpression fuentesSignal(
                QStringLiteral("(?:fuentes|referencias|enlaces|sources):"),
                QRegularExpression::CaseInsensitiveOption);
            if (!finalResponse.contains(fuentesSignal)) {
                QString sourcesBlock = QStringLiteral("\n\n🌐 **Fuentes consultadas:**\n");
                for (const auto &pair : sources) {
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

        for (const auto &pair : sources) {
            QVariantMap s;
            s[QStringLiteral("title")] = pair.first;
            s[QStringLiteral("url")] = pair.second;
            resp.sources.append(s);
        }
        Q_EMIT responseFinished(resp);
    });
    connect(m_geminiClient.get(), &GeminiClient::errorOccurred, this, [this](const QString &errMsg, bool fallbackEligible) {
        if (fallbackEligible && (m_ollamaReachable || readConfigString(QStringLiteral("aiBackend"), QStringLiteral("hybrid")).toLower() == QLatin1String("hybrid"))) {
            qWarning() << "Kity: Gemini failed -> Fallback to Ollama local";
            sendToOllama(m_lastGeminiPrompt, m_lastOllamaContext, m_lastOllamaImage, m_lastHistoryMessages);
            return;
        }
        Q_EMIT errorOccurred(errMsg);
    });
}

LlmEngine::~LlmEngine()
{
    stop();
}

QString LlmEngine::streamingText() const
{
    if (m_ollamaClient && m_ollamaClient->isBusy()) {
        return m_ollamaClient->streamingText();
    }
    if (m_geminiClient && m_geminiClient->isBusy()) {
        return m_geminiClient->streamingText();
    }
    return QString();
}

QString LlmEngine::resolveOllamaUrl()
{
    return readConfigString(QStringLiteral("ollamaUrl"), QStringLiteral("http://127.0.0.1:11434"));
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
    return resolveVisionModel();
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
    return readConfigString(QStringLiteral("systemPrompt"),
        QStringLiteral("Eres Kity, el asistente de voz e inteligencia artificial nativo para CachyOS Linux con KDE Plasma 6 bajo Wayland."));
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
    if (m_ollamaClient) {
        m_ollamaClient->abort();
    }
    if (m_geminiClient) {
        m_geminiClient->abort();
    }
    Q_EMIT streamingTextChanged(QString());
}

void LlmEngine::refreshModels()
{
    if (m_ollamaClient) {
        m_ollamaClient->refreshModels(QUrl(resolveOllamaUrl() + QStringLiteral("/api/tags")));
    }
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
    m_lastGeminiPrompt = userPrompt;

    const QString aiBackend = readConfigString(QStringLiteral("aiBackend"), QStringLiteral("hybrid")).toLower();
    const QString geminiApiKey = resolveGeminiApiKey();

    if (forceGemini && !geminiApiKey.isEmpty()) {
        sendToGemini(userPrompt, injectedContext, base64Image, historyMessages);
        return;
    }

    if (aiBackend == QLatin1String("gemini")) {
        if (!geminiApiKey.isEmpty()) {
            sendToGemini(userPrompt, injectedContext, base64Image, historyMessages);
        } else {
            sendToOllama(userPrompt, injectedContext, base64Image, historyMessages);
        }
        return;
    }

    if (aiBackend == QLatin1String("hybrid")) {
        static const QRegularExpression localActionSignal(
            QStringLiteral("^(?:sube|baja|ajusta|pon|silencia|abre|abrir|ejecuta|cierra|temporizador|alarma|bater[ií]a|brillo|volumen)"),
            QRegularExpression::CaseInsensitiveOption);
        const bool isLocalAction = userPrompt.contains(localActionSignal);

        if (isLocalAction && m_ollamaReachable) {
            sendToOllama(userPrompt, injectedContext, base64Image, historyMessages);
        } else if (!geminiApiKey.isEmpty()) {
            sendToGemini(userPrompt, injectedContext, base64Image, historyMessages);
        } else {
            sendToOllama(userPrompt, injectedContext, base64Image, historyMessages);
        }
        return;
    }

    sendToOllama(userPrompt, injectedContext, base64Image, historyMessages);
}

void LlmEngine::sendToOllama(const QString &userPrompt,
                             const QString &injectedContext,
                             const QString &base64Image,
                             const QVariantList &historyMessages)
{
    Q_EMIT stateChanged(QStringLiteral("thinking"), QStringLiteral("Generando respuesta…"));

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

    const QString vitals = HardwareManager::instance().collectSystemVitals();
    if (!vitals.isEmpty()) {
        fullSysPrompt.append(QStringLiteral("\n[System Environment: %1]").arg(vitals));
    }
    if (!injectedContext.isEmpty()) {
        fullSysPrompt.append(QStringLiteral("\n[Additional Relevant Context:\n%1]").arg(injectedContext.trimmed()));
    }
    sysObj[QStringLiteral("content")] = fullSysPrompt;
    chatMsgs.append(sysObj);

    // 2. Recent history
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

    m_currentChatMessages = chatMsgs;

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
    const int timeoutMs = readConfigInt(QStringLiteral("ollamaTimeoutMs"), 20000);
    m_ollamaClient->sendChat(chatUrl, reqPayload, timeoutMs);
}

void LlmEngine::handleOllamaToolCalls(const QJsonArray &toolCalls)
{
    QJsonObject asstMsg;
    asstMsg[QStringLiteral("role")] = QStringLiteral("assistant");
    asstMsg[QStringLiteral("content")] = QStringLiteral("");
    asstMsg[QStringLiteral("tool_calls")] = toolCalls;
    m_currentChatMessages.append(asstMsg);

    for (const QJsonValue &tcVal : toolCalls) {
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

    const QString modelToUse = resolveOllamaModel();
    QJsonObject opts;
    opts[QStringLiteral("temperature")] = readConfigDouble(QStringLiteral("ollamaTemperature"), 0.2);
    opts[QStringLiteral("num_ctx")] = readConfigInt(QStringLiteral("ollamaNumCtx"), 4096);
    opts[QStringLiteral("num_thread")] = readConfigInt(QStringLiteral("ollamaNumThread"), 6);
    opts[QStringLiteral("num_predict")] = readConfigInt(QStringLiteral("ollamaNumPredict"), 384);

    QJsonObject reqPayload;
    reqPayload[QStringLiteral("model")] = modelToUse;
    reqPayload[QStringLiteral("messages")] = m_currentChatMessages;
    reqPayload[QStringLiteral("stream")] = true;
    reqPayload[QStringLiteral("options")] = opts;
    const QString keepAlive = readConfigString(QStringLiteral("ollamaKeepAlive"), QStringLiteral("15m"));
    reqPayload[QStringLiteral("keep_alive")] = keepAlive.isEmpty() ? QStringLiteral("15m") : keepAlive;

    const QUrl chatUrl(resolveOllamaUrl() + QStringLiteral("/api/chat"));
    const int timeoutMs = readConfigInt(QStringLiteral("ollamaTimeoutMs"), 20000);
    m_ollamaClient->sendChat(chatUrl, reqPayload, timeoutMs);
}

void LlmEngine::sendToGemini(const QString &userPrompt,
                             const QString &injectedContext,
                             const QString &base64Image,
                             const QVariantList &historyMessages)
{
    Q_EMIT stateChanged(QStringLiteral("thinking"), QStringLiteral("Consultando con Google Gemini…"));

    const QString apiKey = resolveGeminiApiKey();
    if (apiKey.isEmpty()) {
        Q_EMIT errorOccurred(QStringLiteral("Clave API de Google Gemini no configurada. Configúrala en los Ajustes de Kity."));
        return;
    }

    const QString geminiModel = resolveGeminiModel();
    const QString modelToUse = (geminiModel.isEmpty() ? QStringLiteral("gemini-1.5-flash") : geminiModel);
    const bool hasVision = !base64Image.isEmpty();

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

    QJsonArray tools;
    QJsonObject searchTool;
    searchTool[QStringLiteral("google_search")] = QJsonObject();
    tools.append(searchTool);
    payload[QStringLiteral("tools")] = tools;

    const QUrl geminiUrl(QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models/%1:streamGenerateContent?key=%2&alt=sse")
        .arg(modelToUse, apiKey));

    m_geminiClient->sendStream(geminiUrl, payload);
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
    return QString();
}

} // namespace Kity
