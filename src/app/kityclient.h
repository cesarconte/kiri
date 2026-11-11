/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QClipboard>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusServiceWatcher>
#include <QGuiApplication>
#include <QObject>
#include <QStringList>
#include <QVariantList>

class KityClient;

class KityConfigHelper : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool wakeWordEnabled READ wakeWordEnabled WRITE setWakeWordEnabled NOTIFY wakeWordEnabledChanged)
    Q_PROPERTY(QString speechOutputMode READ speechOutputMode WRITE setSpeechOutputMode NOTIFY speechOutputModeChanged)
    Q_PROPERTY(bool vadEnabled READ vadEnabled WRITE setVadEnabled NOTIFY vadEnabledChanged)
    Q_PROPERTY(bool earconsEnabled READ earconsEnabled WRITE setEarconsEnabled NOTIFY earconsEnabledChanged)
    Q_PROPERTY(QString ollamaUrl READ ollamaUrl WRITE setOllamaUrl NOTIFY ollamaUrlChanged)
    Q_PROPERTY(bool pin READ pin WRITE setPin NOTIFY pinChanged)

public:
    explicit KityConfigHelper(KityClient *client, QObject *parent = nullptr);

    bool wakeWordEnabled() const;
    void setWakeWordEnabled(bool enabled);

    QString speechOutputMode() const;
    void setSpeechOutputMode(const QString &mode);

    bool vadEnabled() const;
    void setVadEnabled(bool enabled);

    bool earconsEnabled() const;
    void setEarconsEnabled(bool enabled);

    QString ollamaUrl() const { return m_ollamaUrl; }
    void setOllamaUrl(const QString &url);

    bool pin() const { return m_pin; }
    void setPin(bool pinned);

Q_SIGNALS:
    void wakeWordEnabledChanged();
    void speechOutputModeChanged();
    void vadEnabledChanged();
    void earconsEnabledChanged();
    void ollamaUrlChanged();
    void pinChanged();

private:
    KityClient *m_client;
    QString m_ollamaUrl = QStringLiteral("http://127.0.0.1:11434");
    bool m_pin = false;
};

class KityClient : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString streamingText READ streamingText NOTIFY streamingTextChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(QStringList availableModels READ availableModels NOTIFY availableModelsChanged)
    Q_PROPERTY(bool ollamaReachable READ ollamaReachable NOTIFY readinessChanged)
    Q_PROPERTY(bool whisperAvailable READ whisperAvailable NOTIFY readinessChanged)
    Q_PROPERTY(bool piperAvailable READ piperAvailable NOTIFY readinessChanged)
    Q_PROPERTY(bool isRecording READ isRecording NOTIFY stateChanged)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY stateChanged)
    Q_PROPERTY(bool isSpeaking READ isSpeaking NOTIFY stateChanged)
    Q_PROPERTY(qreal audioLevel READ audioLevel NOTIFY audioLevelChanged)
    Q_PROPERTY(bool wakeWordEnabled READ wakeWordEnabled WRITE setWakeWordEnabled NOTIFY wakeWordEnabledChanged)
    Q_PROPERTY(QString speechOutputMode READ speechOutputMode WRITE setSpeechOutputMode NOTIFY speechOutputModeChanged)
    Q_PROPERTY(bool vadEnabled READ vadEnabled WRITE setVadEnabled NOTIFY vadEnabledChanged)
    Q_PROPERTY(bool earconsEnabled READ earconsEnabled WRITE setEarconsEnabled NOTIFY earconsEnabledChanged)
    Q_PROPERTY(QString ollamaModel READ ollamaModel WRITE setOllamaModel NOTIFY ollamaModelChanged)
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectionChanged)
    Q_PROPERTY(QString mobilePairingUrl READ mobilePairingUrl NOTIFY mobilePairingUrlChanged)
    Q_PROPERTY(QObject *configuration READ configuration CONSTANT)

    // macOS 27 Golden Gate: The Siri App Conversations & Voice Preferences
    Q_PROPERTY(QVariantList conversations READ conversations NOTIFY conversationsChanged)
    Q_PROPERTY(QString activeConversationId READ activeConversationId WRITE selectConversation NOTIFY activeConversationIdChanged)
    Q_PROPERTY(QString sidebarViewMode READ sidebarViewMode WRITE setSidebarViewMode NOTIFY goldenGatePrefsChanged)
    Q_PROPERTY(QString siriMode READ siriMode WRITE setSiriMode NOTIFY goldenGatePrefsChanged)
    Q_PROPERTY(QString voiceAccent READ voiceAccent WRITE setVoiceAccent NOTIFY goldenGatePrefsChanged)
    Q_PROPERTY(int voiceIndex READ voiceIndex WRITE setVoiceIndex NOTIFY goldenGatePrefsChanged)
    Q_PROPERTY(qreal voicePace READ voicePace WRITE setVoicePace NOTIFY goldenGatePrefsChanged)
    Q_PROPERTY(qreal voiceExpressivity READ voiceExpressivity WRITE setVoiceExpressivity NOTIFY goldenGatePrefsChanged)
    Q_PROPERTY(QString historyRetention READ historyRetention WRITE setHistoryRetention NOTIFY goldenGatePrefsChanged)
    Q_PROPERTY(int previewLines READ previewLines WRITE setPreviewLines NOTIFY goldenGatePrefsChanged)
    Q_PROPERTY(QString openToMode READ openToMode WRITE setOpenToMode NOTIFY goldenGatePrefsChanged)
    Q_PROPERTY(qreal textScale READ textScale WRITE setTextScale NOTIFY goldenGatePrefsChanged)

public:
    explicit KityClient(QObject *parent = nullptr);
    ~KityClient() override = default;

    QString state() const { return m_state; }
    QString statusMessage() const { return m_statusMessage; }
    QString streamingText() const { return m_streamingText; }
    QVariantList messages() const { return m_messages; }
    QStringList availableModels() const { return m_availableModels; }
    bool ollamaReachable() const { return m_ollamaReachable; }
    bool whisperAvailable() const { return m_whisperAvailable; }
    bool piperAvailable() const { return m_piperAvailable; }
    bool isRecording() const { return m_isRecording; }
    bool isBusy() const { return m_isBusy; }
    bool isSpeaking() const { return m_isSpeaking; }
    qreal audioLevel() const { return m_audioLevel; }
    bool wakeWordEnabled() const { return m_wakeWordEnabled; }
    QString speechOutputMode() const { return m_speechOutputMode; }
    bool vadEnabled() const { return m_vadEnabled; }
    bool earconsEnabled() const { return m_earconsEnabled; }
    QString ollamaModel() const { return m_ollamaModel; }
    bool isConnected() const { return m_connected; }
    QObject *configuration() { return m_configHelper; }

    void setWakeWordEnabled(bool enabled);
    void setSpeechOutputMode(const QString &mode);
    void setVadEnabled(bool enabled);
    void setEarconsEnabled(bool enabled);
    Q_INVOKABLE void setOllamaModel(const QString &model);

    // Q_INVOKABLE methods corresponding to backend functions
    Q_INVOKABLE void sendMessage(const QString &text, bool fromVoice = false);
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void stopSpeaking();
    Q_INVOKABLE void stopGeneration();
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void checkReadiness();
    Q_INVOKABLE void refreshModels();
    Q_INVOKABLE void copyToClipboard(const QString &text) const;
    Q_INVOKABLE void executeCommandInKonsole(const QString &command);
    Q_INVOKABLE void openKnowledgeFolder() const;
    Q_INVOKABLE void openSettings() const;
    Q_INVOKABLE void runVisualIntelligence(const QString &captureMode, const QString &action = QStringLiteral("ocr"), const QString &customQuestion = QString());
    Q_INVOKABLE void applyWritingTool(const QString &action, const QString &customSourceText = QString());
    Q_INVOKABLE QStringList evaluateMathNotes(const QString &text) const;
    Q_INVOKABLE QString renderMarkdown(const QString &markdown) const;
    Q_INVOKABLE void speakText(const QString &text);
    Q_INVOKABLE QStringList extractTerminalCommands(const QString &markdown) const;
    Q_INVOKABLE void controlMedia(const QString &action);
    Q_INVOKABLE void openKOrganizer() const;
    Q_INVOKABLE QString mobilePairingUrl() const;
    Q_INVOKABLE bool shareMobilePairingWithKdeConnect(const QString &deviceId = QString()) const;

    // macOS 27 Golden Gate Methods
    QVariantList conversations() const { return m_conversations; }
    QString activeConversationId() const { return m_activeConversationId; }
    QString sidebarViewMode() const { return m_sidebarViewMode; }
    void setSidebarViewMode(const QString &mode);
    QString siriMode() const { return m_siriMode; }
    void setSiriMode(const QString &mode);
    QString voiceAccent() const { return m_voiceAccent; }
    void setVoiceAccent(const QString &accent);
    int voiceIndex() const { return m_voiceIndex; }
    void setVoiceIndex(int index);
    qreal voicePace() const { return m_voicePace; }
    void setVoicePace(qreal pace);
    qreal voiceExpressivity() const { return m_voiceExpressivity; }
    void setVoiceExpressivity(qreal expr);
    QString historyRetention() const { return m_historyRetention; }
    void setHistoryRetention(const QString &ret);
    int previewLines() const { return m_previewLines; }
    void setPreviewLines(int lines);
    QString openToMode() const { return m_openToMode; }
    void setOpenToMode(const QString &mode);
    qreal textScale() const { return m_textScale; }
    void setTextScale(qreal scale);

    Q_INVOKABLE void newConversation();
    Q_INVOKABLE void selectConversation(const QString &id);
    Q_INVOKABLE void togglePinConversation(const QString &id);
    Q_INVOKABLE void renameConversation(const QString &id, const QString &newTitle);
    Q_INVOKABLE void deleteConversation(const QString &id);
    Q_INVOKABLE void deleteConversations(const QStringList &ids);
    Q_INVOKABLE void attachFiles(const QStringList &fileUrls);
    Q_INVOKABLE void launchSpotlightCategory(const QString &category);
    Q_INVOKABLE void previewSelectedVoice();

public Q_SLOTS:
    void syncFromDBus();

Q_SIGNALS:
    void stateChanged();
    void statusMessageChanged();
    void streamingTextChanged();
    void messagesChanged();
    void availableModelsChanged();
    void readinessChanged();
    void audioLevelChanged();
    void wakeWordEnabledChanged();
    void speechOutputModeChanged();
    void vadEnabledChanged();
    void earconsEnabledChanged();
    void ollamaModelChanged();
    void connectionChanged();
    void transcriptionReady(const QString &text);
    void mobilePairingUrlChanged();
    void conversationsChanged();
    void activeConversationIdChanged();
    void goldenGatePrefsChanged();

private Q_SLOTS:
    void onServiceRegistered(const QString &service);
    void onServiceUnregistered(const QString &service);

private:
    void setupDBus();
    void loadLocalHistory();
    void loadConversations();
    void saveConversations();
    void syncActiveConversationFromMessages();

    QDBusInterface *m_iface = nullptr;
    QDBusServiceWatcher *m_watcher = nullptr;
    KityConfigHelper *m_configHelper = nullptr;

    QString m_state = QStringLiteral("idle");
    QString m_statusMessage;
    QString m_streamingText;
    QVariantList m_messages;
    QStringList m_availableModels;
    bool m_ollamaReachable = false;
    bool m_whisperAvailable = false;
    bool m_piperAvailable = false;
    bool m_isRecording = false;
    bool m_isBusy = false;
    bool m_isSpeaking = false;
    qreal m_audioLevel = 0.0;
    bool m_wakeWordEnabled = true;
    QString m_speechOutputMode = QStringLiteral("always");
    bool m_vadEnabled = true;
    bool m_earconsEnabled = true;
    QString m_ollamaModel = QStringLiteral("llama3.2:3b");
    bool m_connected = false;

    // Golden Gate Conversations & Voice Settings
    QVariantList m_conversations;
    QString m_activeConversationId;
    QString m_sidebarViewMode = QStringLiteral("grid");
    QString m_siriMode = QStringLiteral("ai");
    QString m_voiceAccent = QStringLiteral("Spanish (Spain)");
    int m_voiceIndex = 3; // #4 Red/Pink orb selected by default (matching 10b_siri_voice_window.png)
    qreal m_voicePace = 0.68;
    qreal m_voiceExpressivity = 0.88;
    QString m_historyRetention = QStringLiteral("forever");
    int m_previewLines = 4;
    QString m_openToMode = QStringLiteral("past");
    qreal m_textScale = 1.0;
};
