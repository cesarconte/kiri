/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QString>
#include <QTimer>

class KDictationClient : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool isServiceAvailable READ isServiceAvailable NOTIFY availabilityChanged)
    Q_PROPERTY(bool isEngineReady READ isEngineReady NOTIFY engineReadyChanged)

public:
    static constexpr const char *SERVICE_NAME = "org.cesar.kdictation";
    static constexpr const char *OBJECT_PATH = "/Dictation";
    static constexpr const char *INTERFACE_NAME = "org.cesar.kdictation.Dictation";

    explicit KDictationClient(QObject *parent = nullptr);
    ~KDictationClient() override;

    bool isServiceAvailable() const;
    bool isEngineReady() const;

    Q_INVOKABLE bool checkAvailability();
    Q_INVOKABLE void transcribePcmAsync(const QByteArray &pcmData, const QString &language = QStringLiteral("es"), int timeoutMs = 15000);

Q_SIGNALS:
    void transcriptionReady(const QString &text);
    void transcriptionFailed(const QString &errorMessage);
    void availabilityChanged(bool available);
    void engineReadyChanged(bool ready);

private Q_SLOTS:
    void onServiceRegistered(const QString &serviceName);
    void onServiceUnregistered(const QString &serviceName);
    void onWatcherFinished(QDBusPendingCallWatcher *watcher);
    void onCallTimeout();

private:
    void setAvailable(bool available, bool ready = false);

    QDBusServiceWatcher *m_serviceWatcher = nullptr;
    QDBusPendingCallWatcher *m_activeWatcher = nullptr;
    QTimer *m_callTimeoutTimer = nullptr;
    bool m_isAvailable = false;
    bool m_isEngineReady = false;
};
