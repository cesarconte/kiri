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
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectionChanged)

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
    bool isConnected() const { return m_connected; }

    void setWakeWordEnabled(bool enabled);

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
    Q_INVOKABLE void runVisualIntelligence(const QString &captureMode, const QString &action = QStringLiteral("ocr"), const QString &customQuestion = QString());
    Q_INVOKABLE void applyWritingTool(const QString &action, const QString &customSourceText = QString());
    Q_INVOKABLE QStringList evaluateMathNotes(const QString &text) const;
    Q_INVOKABLE QString renderMarkdown(const QString &markdown) const;
    Q_INVOKABLE void speakText(const QString &text);

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
    void connectionChanged();
    void transcriptionReady(const QString &text);

private Q_SLOTS:
    void onServiceRegistered(const QString &service);
    void onServiceUnregistered(const QString &service);

private:
    void setupDBus();
    void loadLocalHistory();

    QDBusInterface *m_iface = nullptr;
    QDBusServiceWatcher *m_watcher = nullptr;

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
    bool m_connected = false;
};
