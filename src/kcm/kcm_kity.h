/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <KQuickConfigModule>
#include <QNetworkAccessManager>
#include <QStringList>

class KcmKity : public KQuickConfigModule
{
    Q_OBJECT

    // Voice & Audio
    Q_PROPERTY(bool wakeWordEnabled READ wakeWordEnabled WRITE setWakeWordEnabled NOTIFY wakeWordEnabledChanged)
    Q_PROPERTY(QString speechOutputMode READ speechOutputMode WRITE setSpeechOutputMode NOTIFY speechOutputModeChanged)
    Q_PROPERTY(bool earconsEnabled READ earconsEnabled WRITE setEarconsEnabled NOTIFY earconsEnabledChanged)
    Q_PROPERTY(bool vadEnabled READ vadEnabled WRITE setVadEnabled NOTIFY vadEnabledChanged)
    Q_PROPERTY(int vadSilenceTimeoutMs READ vadSilenceTimeoutMs WRITE setVadSilenceTimeoutMs NOTIFY vadSilenceTimeoutMsChanged)
    Q_PROPERTY(bool autoListenOnActivate READ autoListenOnActivate WRITE setAutoListenOnActivate NOTIFY autoListenOnActivateChanged)
    Q_PROPERTY(QString whisperLanguage READ whisperLanguage WRITE setWhisperLanguage NOTIFY whisperLanguageChanged)
    Q_PROPERTY(QString whisperModelPath READ whisperModelPath WRITE setWhisperModelPath NOTIFY whisperModelPathChanged)
    Q_PROPERTY(QString piperVoicePath READ piperVoicePath WRITE setPiperVoicePath NOTIFY piperVoicePathChanged)

    // Ollama AI Engine
    Q_PROPERTY(QString ollamaUrl READ ollamaUrl WRITE setOllamaUrl NOTIFY ollamaUrlChanged)
    Q_PROPERTY(QString ollamaModel READ ollamaModel WRITE setOllamaModel NOTIFY ollamaModelChanged)
    Q_PROPERTY(QString ollamaVisionModel READ ollamaVisionModel WRITE setOllamaVisionModel NOTIFY ollamaVisionModelChanged)
    Q_PROPERTY(double ollamaTemperature READ ollamaTemperature WRITE setOllamaTemperature NOTIFY ollamaTemperatureChanged)
    Q_PROPERTY(int ollamaNumCtx READ ollamaNumCtx WRITE setOllamaNumCtx NOTIFY ollamaNumCtxChanged)
    Q_PROPERTY(int ollamaNumThread READ ollamaNumThread WRITE setOllamaNumThread NOTIFY ollamaNumThreadChanged)
    Q_PROPERTY(int ollamaNumPredict READ ollamaNumPredict WRITE setOllamaNumPredict NOTIFY ollamaNumPredictChanged)
    Q_PROPERTY(QString ollamaKeepAlive READ ollamaKeepAlive WRITE setOllamaKeepAlive NOTIFY ollamaKeepAliveChanged)
    Q_PROPERTY(QString systemPrompt READ systemPrompt WRITE setSystemPrompt NOTIFY systemPromptChanged)

    // Local RAG Knowledge Base
    Q_PROPERTY(bool knowledgeBaseEnabled READ knowledgeBaseEnabled WRITE setKnowledgeBaseEnabled NOTIFY knowledgeBaseEnabledChanged)
    Q_PROPERTY(int knowledgeBaseMaxSnippets READ knowledgeBaseMaxSnippets WRITE setKnowledgeBaseMaxSnippets NOTIFY knowledgeBaseMaxSnippetsChanged)

    // Appearance & Services
    Q_PROPERTY(QString panelIconStyle READ panelIconStyle WRITE setPanelIconStyle NOTIFY panelIconStyleChanged)
    Q_PROPERTY(bool pin READ pin WRITE setPin NOTIFY pinChanged)
    Q_PROPERTY(QString defaultEmailClient READ defaultEmailClient WRITE setDefaultEmailClient NOTIFY defaultEmailClientChanged)
    Q_PROPERTY(QString defaultCalendarClient READ defaultCalendarClient WRITE setDefaultCalendarClient NOTIFY defaultCalendarClientChanged)

    // Live diagnostics & model discovery
    Q_PROPERTY(QStringList detectedModels READ detectedModels NOTIFY detectedModelsChanged)
    Q_PROPERTY(bool ollamaChecking READ ollamaChecking NOTIFY ollamaCheckingChanged)
    Q_PROPERTY(bool ollamaReachable READ ollamaReachable NOTIFY ollamaReachableChanged)
    Q_PROPERTY(bool whisperFound READ whisperFound NOTIFY diagnosticsChanged)
    Q_PROPERTY(bool piperFound READ piperFound NOTIFY diagnosticsChanged)

public:
    explicit KcmKity(QObject *parent, const KPluginMetaData &data);
    ~KcmKity() override;

    void load() override;
    void save() override;
    void defaults() override;

    // Getters & Setters
    bool wakeWordEnabled() const { return m_wakeWordEnabled; }
    void setWakeWordEnabled(bool v);

    QString speechOutputMode() const { return m_speechOutputMode; }
    void setSpeechOutputMode(const QString &v);

    bool earconsEnabled() const { return m_earconsEnabled; }
    void setEarconsEnabled(bool v);

    bool vadEnabled() const { return m_vadEnabled; }
    void setVadEnabled(bool v);

    int vadSilenceTimeoutMs() const { return m_vadSilenceTimeoutMs; }
    void setVadSilenceTimeoutMs(int v);

    bool autoListenOnActivate() const { return m_autoListenOnActivate; }
    void setAutoListenOnActivate(bool v);

    QString whisperLanguage() const { return m_whisperLanguage; }
    void setWhisperLanguage(const QString &v);

    QString whisperModelPath() const { return m_whisperModelPath; }
    void setWhisperModelPath(const QString &v);

    QString piperVoicePath() const { return m_piperVoicePath; }
    void setPiperVoicePath(const QString &v);

    QString ollamaUrl() const { return m_ollamaUrl; }
    void setOllamaUrl(const QString &v);

    QString ollamaModel() const { return m_ollamaModel; }
    void setOllamaModel(const QString &v);

    QString ollamaVisionModel() const { return m_ollamaVisionModel; }
    void setOllamaVisionModel(const QString &v);

    double ollamaTemperature() const { return m_ollamaTemperature; }
    void setOllamaTemperature(double v);

    int ollamaNumCtx() const { return m_ollamaNumCtx; }
    void setOllamaNumCtx(int v);

    int ollamaNumThread() const { return m_ollamaNumThread; }
    void setOllamaNumThread(int v);

    int ollamaNumPredict() const { return m_ollamaNumPredict; }
    void setOllamaNumPredict(int v);

    QString ollamaKeepAlive() const { return m_ollamaKeepAlive; }
    void setOllamaKeepAlive(const QString &v);

    QString systemPrompt() const { return m_systemPrompt; }
    void setSystemPrompt(const QString &v);

    bool knowledgeBaseEnabled() const { return m_knowledgeBaseEnabled; }
    void setKnowledgeBaseEnabled(bool v);

    int knowledgeBaseMaxSnippets() const { return m_knowledgeBaseMaxSnippets; }
    void setKnowledgeBaseMaxSnippets(int v);

    QString panelIconStyle() const { return m_panelIconStyle; }
    void setPanelIconStyle(const QString &v);

    bool pin() const { return m_pin; }
    void setPin(bool v);

    QString defaultEmailClient() const { return m_defaultEmailClient; }
    void setDefaultEmailClient(const QString &v);

    QString defaultCalendarClient() const { return m_defaultCalendarClient; }
    void setDefaultCalendarClient(const QString &v);

    QStringList detectedModels() const { return m_detectedModels; }
    bool ollamaChecking() const { return m_ollamaChecking; }
    bool ollamaReachable() const { return m_ollamaReachable; }
    bool whisperFound() const;
    bool piperFound() const;

    Q_INVOKABLE void fetchModels();
    Q_INVOKABLE void checkReadiness();
    Q_INVOKABLE void openKnowledgeFolder() const;
    Q_INVOKABLE void restoreDefaultPrompt();

Q_SIGNALS:
    void wakeWordEnabledChanged();
    void speechOutputModeChanged();
    void earconsEnabledChanged();
    void vadEnabledChanged();
    void vadSilenceTimeoutMsChanged();
    void autoListenOnActivateChanged();
    void whisperLanguageChanged();
    void whisperModelPathChanged();
    void piperVoicePathChanged();

    void ollamaUrlChanged();
    void ollamaModelChanged();
    void ollamaVisionModelChanged();
    void ollamaTemperatureChanged();
    void ollamaNumCtxChanged();
    void ollamaNumThreadChanged();
    void ollamaNumPredictChanged();
    void ollamaKeepAliveChanged();
    void systemPromptChanged();

    void knowledgeBaseEnabledChanged();
    void knowledgeBaseMaxSnippetsChanged();

    void panelIconStyleChanged();
    void pinChanged();
    void defaultEmailClientChanged();
    void defaultCalendarClientChanged();

    void detectedModelsChanged();
    void ollamaCheckingChanged();
    void ollamaReachableChanged();
    void diagnosticsChanged();

private:
    void notifyDaemon();

    QNetworkAccessManager *m_nam = nullptr;

    bool m_wakeWordEnabled = true;
    QString m_speechOutputMode = QStringLiteral("always");
    bool m_earconsEnabled = true;
    bool m_vadEnabled = true;
    int m_vadSilenceTimeoutMs = 1300;
    bool m_autoListenOnActivate = false;
    QString m_whisperLanguage = QStringLiteral("es");
    QString m_whisperModelPath;
    QString m_piperVoicePath;

    QString m_ollamaUrl = QStringLiteral("http://127.0.0.1:11434");
    QString m_ollamaModel = QStringLiteral("llama3.2:3b");
    QString m_ollamaVisionModel = QStringLiteral("llama3.2:3b");
    double m_ollamaTemperature = 0.2;
    int m_ollamaNumCtx = 4096;
    int m_ollamaNumThread = 6;
    int m_ollamaNumPredict = 384;
    QString m_ollamaKeepAlive = QStringLiteral("24h");
    QString m_systemPrompt;

    bool m_knowledgeBaseEnabled = true;
    int m_knowledgeBaseMaxSnippets = 3;

    QString m_panelIconStyle = QStringLiteral("symbolic");
    bool m_pin = false;
    QString m_defaultEmailClient = QStringLiteral("proton");
    QString m_defaultCalendarClient = QStringLiteral("proton");

    QStringList m_detectedModels;
    bool m_ollamaChecking = false;
    bool m_ollamaReachable = false;
};
