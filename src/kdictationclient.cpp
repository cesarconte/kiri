/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "kdictationclient.h"

#include <QDebug>

KDictationClient::KDictationClient(QObject *parent)
    : QObject(parent)
    , m_callTimeoutTimer(new QTimer(this))
{
    m_callTimeoutTimer->setSingleShot(true);
    connect(m_callTimeoutTimer, &QTimer::timeout, this, &KDictationClient::onCallTimeout);

    m_serviceWatcher = new QDBusServiceWatcher(
        QLatin1String(SERVICE_NAME),
        QDBusConnection::sessionBus(),
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration,
        this);

    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceRegistered,
            this, &KDictationClient::onServiceRegistered);
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, &KDictationClient::onServiceUnregistered);

    checkAvailability();
}

KDictationClient::~KDictationClient()
{
    if (m_callTimeoutTimer && m_callTimeoutTimer->isActive()) {
        m_callTimeoutTimer->stop();
    }
    if (m_activeWatcher) {
        delete m_activeWatcher;
        m_activeWatcher = nullptr;
    }
}

bool KDictationClient::isServiceAvailable() const
{
    return m_isAvailable && m_isEngineReady;
}

bool KDictationClient::isEngineReady() const
{
    return m_isEngineReady;
}

void KDictationClient::setAvailable(bool available, bool ready)
{
    const bool prevEffective = isServiceAvailable();
    const bool prevAvailable = m_isAvailable;
    const bool prevReady = m_isEngineReady;

    m_isAvailable = available;
    m_isEngineReady = ready;

    if (m_isEngineReady != prevReady) {
        Q_EMIT engineReadyChanged(m_isEngineReady);
    }
    if (isServiceAvailable() != prevEffective || m_isAvailable != prevAvailable) {
        Q_EMIT availabilityChanged(isServiceAvailable());
    }
}

bool KDictationClient::checkAvailability()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        setAvailable(false, false);
        return false;
    }

    QDBusConnectionInterface *busIface = bus.interface();
    if (!busIface || !busIface->isServiceRegistered(QLatin1String(SERVICE_NAME))) {
        setAvailable(false, false);
        return false;
    }

    QDBusInterface iface(
        QLatin1String(SERVICE_NAME),
        QLatin1String(OBJECT_PATH),
        QLatin1String(INTERFACE_NAME),
        bus);

    if (!iface.isValid()) {
        setAvailable(true, false);
        return false;
    }

    // Comprobación rápida y ligera de salud del motor STT con timeout corto (350 ms)
    iface.setTimeout(350);
    QDBusReply<bool> readyReply = iface.call(QStringLiteral("IsEngineReady"));
    const bool ready = readyReply.isValid() && readyReply.value();

    setAvailable(true, ready);
    return ready;
}

void KDictationClient::onServiceRegistered(const QString &serviceName)
{
    Q_UNUSED(serviceName);
    qInfo() << "KDictationClient: Servicio" << SERVICE_NAME << "registrado en D-Bus. Verificando disponibilidad…";
    checkAvailability();
}

void KDictationClient::onServiceUnregistered(const QString &serviceName)
{
    Q_UNUSED(serviceName);
    qInfo() << "KDictationClient: Servicio" << SERVICE_NAME << "desconectado de D-Bus.";
    setAvailable(false, false);
}

void KDictationClient::transcribePcmAsync(const QByteArray &pcmData, const QString &language, int timeoutMs)
{
    if (pcmData.isEmpty()) {
        Q_EMIT transcriptionFailed(QStringLiteral("Audio PCM vacío."));
        return;
    }

    if (m_activeWatcher) {
        m_activeWatcher->deleteLater();
        m_activeWatcher = nullptr;
    }
    m_callTimeoutTimer->stop();

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        setAvailable(false, false);
        Q_EMIT transcriptionFailed(QStringLiteral("D-Bus no está conectado."));
        return;
    }

    QDBusInterface iface(
        QLatin1String(SERVICE_NAME),
        QLatin1String(OBJECT_PATH),
        QLatin1String(INTERFACE_NAME),
        bus);

    if (!iface.isValid()) {
        setAvailable(false, false);
        Q_EMIT transcriptionFailed(QStringLiteral("Interfaz D-Bus de KDictation inválida."));
        return;
    }

    iface.setTimeout(timeoutMs);

    const QString lang = language.trimmed().isEmpty() ? QStringLiteral("es") : language.trimmed();
    QDBusPendingCall pcall = iface.asyncCall(QStringLiteral("TranscribePcm"), pcmData, lang);

    m_activeWatcher = new QDBusPendingCallWatcher(pcall, this);
    connect(m_activeWatcher, &QDBusPendingCallWatcher::finished,
            this, &KDictationClient::onWatcherFinished);

    m_callTimeoutTimer->start(timeoutMs);
}

void KDictationClient::onWatcherFinished(QDBusPendingCallWatcher *watcher)
{
    m_callTimeoutTimer->stop();

    if (watcher != m_activeWatcher) {
        watcher->deleteLater();
        return;
    }
    m_activeWatcher = nullptr;

    QDBusPendingReply<QString> reply = *watcher;
    watcher->deleteLater();

    if (reply.isError()) {
        qWarning() << "KDictationClient: Error en D-Bus TranscribePcm:" << reply.error().message();
        Q_EMIT transcriptionFailed(reply.error().message());
        return;
    }

    const QString transcript = reply.value();
    Q_EMIT transcriptionReady(transcript);
}

void KDictationClient::transcribeAndInjectPcmAsync(const QByteArray &pcmData, const QString &language, int timeoutMs)
{
    if (pcmData.isEmpty()) {
        Q_EMIT transcriptionFailed(QStringLiteral("Audio PCM vacío."));
        return;
    }

    if (m_dictationWatcher) {
        m_dictationWatcher->deleteLater();
        m_dictationWatcher = nullptr;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        Q_EMIT transcriptionFailed(QStringLiteral("D-Bus no está conectado."));
        return;
    }

    QDBusInterface iface(
        QLatin1String(SERVICE_NAME),
        QLatin1String(OBJECT_PATH),
        QLatin1String(INTERFACE_NAME),
        bus);

    iface.setTimeout(timeoutMs);

    const QString lang = language.trimmed().isEmpty() ? QStringLiteral("es") : language.trimmed();
    QDBusPendingCall pcall = iface.asyncCall(QStringLiteral("TranscribeAndInjectPcm"), pcmData, lang);

    m_dictationWatcher = new QDBusPendingCallWatcher(pcall, this);
    connect(m_dictationWatcher, &QDBusPendingCallWatcher::finished,
            this, &KDictationClient::onDictationWatcherFinished);
}

void KDictationClient::onDictationWatcherFinished(QDBusPendingCallWatcher *watcher)
{
    if (watcher != m_dictationWatcher) {
        watcher->deleteLater();
        return;
    }
    m_dictationWatcher = nullptr;

    QDBusPendingReply<QString> reply = *watcher;
    watcher->deleteLater();

    if (reply.isError()) {
        qWarning() << "KDictationClient: Error en D-Bus TranscribeAndInjectPcm:" << reply.error().message();
        Q_EMIT transcriptionFailed(reply.error().message());
        return;
    }

    const QString transcript = reply.value();
    Q_EMIT dictationInjected(transcript);
}

void KDictationClient::injectText(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return;
    }

    QDBusInterface iface(
        QLatin1String(SERVICE_NAME),
        QLatin1String(OBJECT_PATH),
        QLatin1String(INTERFACE_NAME),
        bus);

    iface.asyncCall(QStringLiteral("InjectText"), text.trimmed());
    Q_EMIT dictationInjected(text.trimmed());
}

void KDictationClient::onCallTimeout()
{
    if (m_activeWatcher) {
        m_activeWatcher->deleteLater();
        m_activeWatcher = nullptr;
    }

    qWarning() << "KDictationClient: Timeout esperando respuesta de KDictation en D-Bus.";
    Q_EMIT transcriptionFailed(QStringLiteral("Timeout en llamada D-Bus a KDictation."));
}
