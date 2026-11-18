/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "kdeconnectmanager.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QRegularExpression>
#include <KLocalizedString>

namespace Kity {

KdeConnectManager &KdeConnectManager::instance()
{
    static KdeConnectManager s_instance;
    return s_instance;
}

KdeConnectManager::KdeConnectManager(QObject *parent)
    : QObject(parent)
{
}

QList<KdeConnectDeviceInfo> KdeConnectManager::getDevices() const
{
    QList<KdeConnectDeviceInfo> result;
    QDBusInterface iface(QStringLiteral("org.kde.kdeconnect"),
                         QStringLiteral("/modules/kdeconnect"),
                         QStringLiteral("org.kde.kdeconnect.daemon"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid()) return result;

    QDBusReply<QStringList> reply = iface.call(QStringLiteral("devices"), true, true);
    if (!reply.isValid()) {
        reply = iface.call(QStringLiteral("devices"));
    }

    if (reply.isValid()) {
        for (const QString &devId : reply.value()) {
            QDBusInterface devIface(QStringLiteral("org.kde.kdeconnect"),
                                    QStringLiteral("/modules/kdeconnect/devices/") + devId,
                                    QStringLiteral("org.kde.kdeconnect.device"),
                                    QDBusConnection::sessionBus());
            QString devName = devId;
            if (devIface.isValid()) {
                const QVariant nameProp = devIface.property("name");
                if (nameProp.isValid() && !nameProp.toString().isEmpty()) {
                    devName = nameProp.toString();
                }
            }
            result.append({devId, devName});
        }
    }
    return result;
}

QString KdeConnectManager::getBatteryInfo(const QString &deviceId) const
{
    QDBusInterface iface(QStringLiteral("org.kde.kdeconnect"),
                         QStringLiteral("/modules/kdeconnect/devices/") + deviceId + QStringLiteral("/battery"),
                         QStringLiteral("org.kde.kdeconnect.device.battery"),
                         QDBusConnection::sessionBus());
    if (iface.isValid()) {
        const QVariant charge = iface.property("charge");
        const QVariant isCharging = iface.property("isCharging");
        if (charge.isValid()) {
            const int pct = charge.toInt();
            const bool charging = isCharging.isValid() ? isCharging.toBool() : false;
            return i18n("La batería de tu teléfono está al %1%%2.", pct, charging ? i18n(" (cargando)") : QString());
        }
    }
    return QString();
}

bool KdeConnectManager::ringDevice(const QString &deviceId) const
{
    QDBusInterface iface(QStringLiteral("org.kde.kdeconnect"),
                         QStringLiteral("/modules/kdeconnect/devices/") + deviceId + QStringLiteral("/findmyphone"),
                         QStringLiteral("org.kde.kdeconnect.device.findmyphone"),
                         QDBusConnection::sessionBus());
    if (iface.isValid()) {
        QDBusReply<void> reply = iface.call(QStringLiteral("ring"));
        return reply.isValid();
    }
    return false;
}

bool KdeConnectManager::sendSms(const QString &deviceId, const QString &phoneNumber, const QString &message) const
{
    if (isRestrictedPhoneNumber(phoneNumber)) return false;

    QDBusInterface iface(QStringLiteral("org.kde.kdeconnect"),
                         QStringLiteral("/modules/kdeconnect/devices/") + deviceId + QStringLiteral("/sms"),
                         QStringLiteral("org.kde.kdeconnect.device.sms"),
                         QDBusConnection::sessionBus());
    if (iface.isValid()) {
        QDBusReply<void> reply = iface.call(QStringLiteral("sendSms"), phoneNumber, message);
        return reply.isValid();
    }
    return false;
}

bool KdeConnectManager::sendClipboard(const QString &deviceId, const QString &text) const
{
    QDBusInterface iface(QStringLiteral("org.kde.kdeconnect"),
                         QStringLiteral("/modules/kdeconnect/devices/") + deviceId + QStringLiteral("/clipboard"),
                         QStringLiteral("org.kde.kdeconnect.device.clipboard"),
                         QDBusConnection::sessionBus());
    if (iface.isValid()) {
        QDBusReply<void> reply = iface.call(QStringLiteral("sendClipboardMessage"), text);
        return reply.isValid();
    }
    return false;
}

bool KdeConnectManager::initiateCall(const QString &deviceId, const QString &phoneNumber) const
{
    if (isRestrictedPhoneNumber(phoneNumber)) return false;
    const QString cleanNum = sanitizePhoneForDial(phoneNumber);

    QDBusInterface iface(QStringLiteral("org.kde.kdeconnect"),
                         QStringLiteral("/modules/kdeconnect/devices/") + deviceId + QStringLiteral("/telephony"),
                         QStringLiteral("org.kde.kdeconnect.device.telephony"),
                         QDBusConnection::sessionBus());
    if (iface.isValid()) {
        QDBusReply<void> reply = iface.call(QStringLiteral("dial"), cleanNum);
        return reply.isValid();
    }
    return false;
}

bool KdeConnectManager::triggerSmartDucking(bool duck) const
{
    const auto devices = getDevices();
    bool triggered = false;
    for (const auto &dev : devices) {
        QDBusInterface duckIface(QStringLiteral("org.kde.kdeconnect"),
                                 QStringLiteral("/modules/kdeconnect/devices/") + dev.id + QStringLiteral("/smartducking"),
                                 QStringLiteral("org.kde.kdeconnect.device.smartducking"),
                                 QDBusConnection::sessionBus());
        if (duckIface.isValid()) {
            duckIface.call(QDBus::NoBlock, QStringLiteral("triggerDucking"), duck);
            triggered = true;
        }
    }
    return triggered;
}

QString KdeConnectManager::normalizePhoneNumber(const QString &raw)
{
    QString res = raw;
    res.remove(QRegularExpression(QStringLiteral("[^0-9+]")));
    return res;
}

QString KdeConnectManager::sanitizePhoneForDial(const QString &raw)
{
    return normalizePhoneNumber(raw);
}

bool KdeConnectManager::isRestrictedPhoneNumber(const QString &phone)
{
    const QString clean = normalizePhoneNumber(phone);
    return clean == QStringLiteral("112") || clean == QStringLiteral("911") || clean == QStringLiteral("999");
}

} // namespace Kity
