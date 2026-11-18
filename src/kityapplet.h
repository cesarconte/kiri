/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <Plasma/Applet>
#include <QDateTime>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QVariantList>

#include "system/configmanager.h"
#include "kdictationclient.h"

class KityMobileServer;

class KityApplet : public Plasma::Applet
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.cesar.kity")

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
    Q_PROPERTY(QString panelIconStyle READ panelIconStyle WRITE setPanelIconStyle NOTIFY panelIconStyleChanged)
    Q_PROPERTY(bool vadEnabled READ vadEnabled WRITE setVadEnabled NOTIFY vadEnabledChanged)
    Q_PROPERTY(bool earconsEnabled READ earconsEnabled WRITE setEarconsEnabled NOTIFY earconsEnabledChanged)
    Q_PROPERTY(bool useCentralizedVoice READ useCentralizedVoice WRITE setUseCentralizedVoice NOTIFY useCentralizedVoiceChanged)
    Q_PROPERTY(bool kdictationAvailable READ isKDictationAvailable NOTIFY kdictationAvailableChanged)
    Q_PROPERTY(QString ollamaModel READ ollamaModel WRITE setOllamaModel NOTIFY ollamaModelChanged)
    Q_PROPERTY(QString mobilePairingUrl READ mobilePairingUrl NOTIFY mobilePairingUrlChanged)

public:
    struct ContactEntry {
        QString name;
        QString phone;
        QString email;
    };

    struct ScheduledReminder {
        QString id;
        QString text;
        QDateTime targetTime;
        bool notified = false;
    };

    struct KdeConnectDevice {
        QString id;
        QString name;
    };

    struct ParsedCalendarEvent {
        QString title;
        QDateTime startTime;
        QDateTime endTime;
        QString description;
    };

    explicit KityApplet(QObject *parent, const KPluginMetaData &data, const QVariantList &args);
    ~KityApplet() override;

    void init() override;
    void configChanged() override;

    // Property Accessors
    QString state() const;
    QString statusMessage() const;
    QString streamingText() const;
    QVariantList messages() const;
    QStringList availableModels() const;
    bool ollamaReachable() const;
    bool whisperAvailable() const;
    bool piperAvailable() const;
    bool isRecording() const;
    bool isBusy() const;
    bool isSpeaking() const;
    qreal audioLevel() const;

    bool wakeWordEnabled() const;
    void setWakeWordEnabled(bool enabled);
    QString speechOutputMode() const;
    void setSpeechOutputMode(const QString &mode);
    QString panelIconStyle() const;
    Q_INVOKABLE void setPanelIconStyle(const QString &style);
    bool vadEnabled() const;
    void setVadEnabled(bool enabled);
    bool earconsEnabled() const;
    void setEarconsEnabled(bool enabled);
    bool useCentralizedVoice() const;
    void setUseCentralizedVoice(bool enabled);
    bool isKDictationAvailable() const;
    Q_INVOKABLE bool checkKDictationAvailable();
    QString ollamaModel() const;
    Q_INVOKABLE void setOllamaModel(const QString &model);

    // Q_INVOKABLE Core Actions
    Q_INVOKABLE void sendMessage(const QString &text, bool fromVoice = false);
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void stopGeneration();
    Q_INVOKABLE void speakText(const QString &text);
    Q_INVOKABLE void stopSpeaking();
    Q_INVOKABLE void startWakeWordListener();
    Q_INVOKABLE void stopWakeWordListener();
    Q_INVOKABLE void openUI();
    Q_INVOKABLE void toggleUI();
    Q_INVOKABLE void openSettings();
    Q_INVOKABLE void reloadConfig();
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void refreshModels();
    Q_INVOKABLE void checkReadiness();

    // Q_INVOKABLE Subsystem Delegations
    Q_INVOKABLE bool launchApplication(const QString &appQuery, QString *launchedNameOut = nullptr);
    Q_INVOKABLE bool closeApplication(const QString &appQuery, QString *closedNameOut = nullptr);
    Q_INVOKABLE QStringList extractTerminalCommands(const QString &markdown) const;
    Q_INVOKABLE void executeCommandInKonsole(const QString &command);
    Q_INVOKABLE void copyToClipboard(const QString &text) const;
    Q_INVOKABLE void replaceText(const QString &text) const;
    Q_INVOKABLE QString renderMarkdown(const QString &markdown) const;
    Q_INVOKABLE void openKnowledgeFolder() const;
    Q_INVOKABLE int knowledgeDocumentCount() const;

    // Apple Intelligence Writing Tools
    Q_INVOKABLE QString readWaylandSelectionOrClipboard() const;
    Q_INVOKABLE void applyWritingTool(const QString &action, const QString &customSourceText = QString());
    Q_INVOKABLE static QString buildWritingToolPrompt(const QString &action, const QString &sourceText);
    Q_INVOKABLE void translateSelection(const QString &targetLanguage = QStringLiteral("es"));
    Q_INVOKABLE static QString languageDisplayName(const QString &code);
    Q_INVOKABLE QStringList evaluateMathNotes(const QString &text) const;

    // Visual Intelligence & Live Text
    Q_INVOKABLE void runVisualIntelligence(const QString &captureMode, const QString &action = QStringLiteral("ocr"), const QString &customQuestion = QString());
    Q_INVOKABLE static QVariantMap parseLiveTextEntities(const QString &ocrText);
    Q_INVOKABLE QString collectKWinWindowsInfo() const;

    // Generative UI & System Integrations
    Q_INVOKABLE void controlMedia(const QString &action);
    Q_INVOKABLE void openKOrganizer() const;
    Q_INVOKABLE void sendPendingEmail();
    Q_INVOKABLE void openPendingEmailInProton() const;
    Q_INVOKABLE void discardPendingEmail();
    Q_INVOKABLE QVariantMap queryActiveMedia() const;
    Q_INVOKABLE QVariantMap fetchWeatherData(const QString &query) const;
    Q_INVOKABLE bool createCalendarEvent(const QString &title, const QString &datePrompt);
    Q_INVOKABLE void composeEmail(const QString &to, const QString &subject, const QString &body, const QString &service = QStringLiteral("proton")) const;

    // Mobile Remote & Portal
    Q_INVOKABLE QString mobilePairingUrl() const;
    Q_INVOKABLE bool shareMobilePairingWithKdeConnect(const QString &deviceId = QString()) const;

Q_SIGNALS:
    void stateChanged();
    void statusMessageChanged();
    void streamingTextChanged();
    void messagesChanged();
    void availableModelsChanged();
    void readinessChanged();
    void audioLevelChanged();
    void transcriptionReady(const QString &text);
    void wakeWordTriggered();
    void mobilePairingUrlChanged();
    void openRequested();
    void toggleRequested();
    void wakeWordEnabledChanged();
    void panelIconStyleChanged();
    void speechOutputModeChanged();
    void vadEnabledChanged();
    void earconsEnabledChanged();
    void useCentralizedVoiceChanged();
    void kdictationAvailableChanged();
    void ollamaModelChanged();

private:
    void setState(const QString &newState, const QString &newStatus = QString());
    void appendAssistantMessage(const QString &text, bool speakIfAppropriate = true);
    bool detectAndExecuteSystemAction(const QString &input);

    // Storage persistence
    void loadConversationHistory();
    void saveConversationHistory();

    // State members
    QString m_state = QStringLiteral("idle");
    QString m_statusMessage;
    QString m_streamingText;
    QVariantList m_messages;
    QStringList m_availableModels;
    qreal m_audioLevel = 0.0;
    bool m_ollamaReachable = false;
    bool m_whisperAvailable = false;
    bool m_piperAvailable = false;
    bool m_wakeWordEnabled = false;

    // Timers & Network
    QNetworkAccessManager *m_nam = nullptr;
    QTimer *m_reminderTimer = nullptr;
    QTimer *m_readinessTimer = nullptr;
    bool m_lastQueryWasVoice = false;

    // Generative UI pending cards
    QString m_pendingCardType;
    QVariantMap m_pendingCardData;

    // Mobile Remote Server
    KityMobileServer *m_mobileServer = nullptr;
};
