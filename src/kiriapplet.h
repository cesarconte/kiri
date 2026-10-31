/*
    SPDX-FileCopyrightText: 2026 Kiri contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <Plasma/Applet>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QQueue>
#include <QRegularExpression>
#include <QTimer>
#include <QVariantList>

class KiriApplet : public Plasma::Applet
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

    explicit KiriApplet(QObject *parent, const KPluginMetaData &data, const QVariantList &args);
    ~KiriApplet() override;

    void init() override;
    void configChanged() override;

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

    // Q_INVOKABLE Methods
    Q_INVOKABLE void sendMessage(const QString &text, bool fromVoice = false);
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void stopGeneration();
    Q_INVOKABLE void speakText(const QString &text);
    Q_INVOKABLE void stopSpeaking();
    Q_INVOKABLE void startWakeWordListener();
    Q_INVOKABLE void stopWakeWordListener();
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void refreshModels();
    Q_INVOKABLE void checkReadiness();

    Q_INVOKABLE bool launchApplication(const QString &appQuery, QString *launchedNameOut = nullptr);
    Q_INVOKABLE bool closeApplication(const QString &appQuery, QString *closedNameOut = nullptr);
    Q_INVOKABLE QStringList extractTerminalCommands(const QString &markdown) const;
    Q_INVOKABLE void executeCommandInKonsole(const QString &command);
    Q_INVOKABLE void copyToClipboard(const QString &text) const;
    Q_INVOKABLE QString renderMarkdown(const QString &markdown) const;
    Q_INVOKABLE void openKnowledgeFolder() const;
    Q_INVOKABLE int knowledgeDocumentCount() const;

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
    void wakeWordEnabledChanged();

private Q_SLOTS:
    void handleOllamaStreamChunk();
    void handleOllamaFinished();
    void checkVoiceActivity();
    void checkScheduledReminders();
    void onWakeWordAudioChunkReady();

private:
    void setState(const QString &newState, const QString &newStatus = QString());
    void sendToOllama(const QString &userPrompt, const QString &injectedContext = QString(), const QString &base64Image = QString());
    bool detectAndExecuteSystemAction(const QString &input);

    // Speech & Audio
    void runWhisperTranscription();
    void checkWakeWordTranscription(const QString &wavPath);
    void enqueueSentenceForSpeech(const QString &sentence);
    void playNextTtsQueueItem();
    void speakSentence(const QString &sentence);
    void processStreamingTtsChunk(const QString &newChunk);
    QString stripMarkdownForSpeech(const QString &markdown) const;
    void startVad();
    void stopVad();
    QByteArray createWavHeader(int dataSize, int sampleRate, int channels, int bitsPerSample) const;

    // Config readers
    QString readConfigString(const QString &key, const QString &def = QString()) const;
    int readConfigInt(const QString &key, int def = 0) const;
    double readConfigDouble(const QString &key, double def = 0.0) const;
    bool readConfigBool(const QString &key, bool def = false) const;

    QString resolveOllamaUrl() const;
    QString resolveOllamaModel() const;
    QString resolveVisionModel() const;
    QString resolveSystemPrompt() const;
    QString resolveWhisperModelPath() const;
    QString resolveWhisperLanguage() const;
    QString resolvePiperVoicePath() const;
    QString resolveSpeechOutputMode() const;

    // Context retrievers
    QString fetchWebSearchContext(const QString &query) const;
    QString fetchWeatherContext(const QString &query) const;
    QString fetchDynamicLocalContext(const QString &query, const QString &intent) const;
    QString fetchKnowledgeBaseContext(const QString &query) const;
    QString collectSystemVitals() const;
    QString detectAvailableVisionModel() const;
    QString captureScreenToPng(const QString &destPath) const;
    QString encodeImageToBase64(const QString &imagePath, int maxDimension = 1024) const;

    // KDE & System Integrations
    QString getActivePowerProfile() const;
    bool setActivePowerProfile(const QString &profile) const;
    QString getLocalBatteryInfo() const;
    bool isDoNotDisturbActive() const;
    bool setDoNotDisturb(bool active) const;
    QString extractExecBinaryName(const QString &execLine) const;

    // Calendar, Contacts & KDE Connect
    ParsedCalendarEvent parseCalendarEventPrompt(const QString &prompt, const QDateTime &now) const;
    int monthNameToNumber(const QString &name) const;
    bool createCalendarEvent(const QString &title, const QString &datePrompt);
    void openCalendarService(const ParsedCalendarEvent &ev) const;
    QUrl buildGoogleCalendarTemplateUrl(const ParsedCalendarEvent &ev) const;
    void composeEmail(const QString &to, const QString &subject, const QString &body, const QString &service) const;

    QList<KdeConnectDevice> getKdeConnectDevices() const;
    QString getKdeConnectBattery(const QString &deviceId) const;
    bool ringKdeConnectDevice(const QString &deviceId) const;
    bool sendTextToKdeConnect(const QString &deviceId, const QString &phoneNumber, const QString &message) const;
    bool sendClipboardToKdeConnect(const QString &deviceId, const QString &text) const;
    bool initiatePhoneCall(const QString &deviceId, const QString &phoneNumber) const;
    QString normalizePhoneNumber(const QString &raw) const;
    QString sanitizePhoneForDial(const QString &raw) const;
    bool isRestrictedPhoneNumber(const QString &phone) const;

    // Storage paths & persistence
    QString historyFilePath() const;
    QString contactsFilePath() const;
    QString remindersFilePath() const;
    QString scheduledRemindersFilePath() const;

    void loadConversationHistory();
    void saveConversationHistory();
    void loadContacts();
    void saveContacts();
    void importContactsFromVcf(const QString &vcfPath);
    ContactEntry findContactByName(const QString &query) const;
    QString normalizeForMatch(const QString &text) const;

    void loadReminders();
    void saveReminders();
    void loadScheduledReminders();
    void saveScheduledReminders();
    void triggerReminderNotification(const ScheduledReminder &reminder);

    // HTTP Helpers
    QByteArray httpGetSync(const QUrl &url, int timeoutMs = 4000) const;
    QByteArray httpPostJsonSync(const QUrl &url, const QJsonObject &payload, int timeoutMs = 8000) const;

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

    // Network & Process
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_currentOllamaReply = nullptr;
    QProcess *m_recordProcess = nullptr;
    QProcess *m_ttsProcess = nullptr;
    QProcess *m_wakeWordProcess = nullptr;
    QTimer *m_vadTimer = nullptr;
    QTimer *m_reminderTimer = nullptr;
    QTimer *m_readinessTimer = nullptr;

    QElapsedTimer m_speechSilenceTimer;
    bool m_speechDetected = false;
    QString m_currentRecordingPath;
    QString m_ttsSpeechBuffer;
    QQueue<QString> m_ttsQueue;
    bool m_isSpeakingSentence = false;
    bool m_lastQueryWasVoice = false;

    QList<ContactEntry> m_contacts;
    QList<ScheduledReminder> m_scheduledReminders;
};
