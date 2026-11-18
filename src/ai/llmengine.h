/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

#include "geminiclient.h"
#include "ollamaclient.h"

namespace Kity {

struct LlmResponse {
    QString text;
    QVariantList sources;
    QString cardType;
    QVariantMap cardData;
    bool isWritingTool = false;
    QString sourceText;
    QString writingToolAction;
};

/**
 * @brief LlmEngine orchestrates LLM interactions (local Ollama streaming and cloud Google Gemini streaming),
 * tool invocation loops, context grounding, and model discovery.
 */
class LlmEngine : public QObject
{
    Q_OBJECT

public:
    static LlmEngine &instance();

    void query(const QString &userPrompt,
               const QString &injectedContext = QString(),
               const QString &base64Image = QString(),
               const QVariantList &historyMessages = QVariantList(),
               bool forceGemini = false);

    void stop();
    void refreshModels();

    QString streamingText() const;
    QStringList availableModels() const { return m_availableModels; }
    bool isOllamaReachable() const { return m_ollamaReachable; }

    // Configuration resolution helpers
    static QString resolveOllamaUrl();
    static QString resolveOllamaModel();
    static QString resolveVisionModel();
    static QString detectAvailableVisionModel();
    static QString resolveGeminiApiKey();
    static QString resolveGeminiModel();
    static QString resolveSystemPrompt();

    // Text & Vision Analysis
    QString analyzeImageWithVisionModel(const QString &base64Image);

    // Context & Tool Execution
    void setPendingCard(const QString &cardType, const QVariantMap &cardData);
    void setWritingToolContext(bool isWritingTool, const QString &sourceText, const QString &action);

Q_SIGNALS:
    void stateChanged(const QString &state, const QString &statusMessage);
    void streamingTextChanged(const QString &fullText);
    void streamingChunkReceived(const QString &chunk);
    void responseFinished(const Kity::LlmResponse &response);
    void errorOccurred(const QString &errorMessage);
    void availableModelsChanged(const QStringList &models);
    void ollamaReachabilityChanged(bool reachable);

private:
    explicit LlmEngine(QObject *parent = nullptr);
    ~LlmEngine() override;

    LlmEngine(const LlmEngine &) = delete;
    LlmEngine &operator=(const LlmEngine &) = delete;

    void sendToOllama(const QString &userPrompt,
                      const QString &injectedContext,
                      const QString &base64Image,
                      const QVariantList &historyMessages);

    void sendToGemini(const QString &userPrompt,
                      const QString &injectedContext,
                      const QString &base64Image,
                      const QVariantList &historyMessages);

    QJsonObject executeToolCall(const QString &toolName, const QJsonObject &arguments);
    void handleOllamaToolCalls(const QJsonArray &toolCalls);

    QNetworkAccessManager *m_nam = nullptr;
    std::unique_ptr<OllamaClient> m_ollamaClient;
    std::unique_ptr<GeminiClient> m_geminiClient;

    QString m_lastOllamaPrompt;
    QString m_lastOllamaContext;
    QString m_lastOllamaImage;
    QVariantList m_lastHistoryMessages;

    QString m_lastGeminiPrompt;
    QJsonArray m_currentChatMessages;

    QStringList m_availableModels;
    bool m_ollamaReachable = false;

    QString m_pendingCardType;
    QVariantMap m_pendingCardData;

    bool m_lastWasWritingTool = false;
    QString m_lastWritingToolSource;
    QString m_lastWritingToolAction;
};

} // namespace Kity
