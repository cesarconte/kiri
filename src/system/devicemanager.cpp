/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "devicemanager.h"

#include <KLocalizedString>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QProcess>
#include <QStandardPaths>

namespace Kity {

DeviceManager &DeviceManager::instance()
{
    static DeviceManager s_instance;
    return s_instance;
}

DeviceManager::DeviceManager(QObject *parent)
    : QObject(parent)
{
}

QString DeviceManager::getActivePowerProfile() const
{
    QDBusInterface iface(QStringLiteral("net.hadess.PowerProfiles"),
                         QStringLiteral("/net/hadess/PowerProfiles"),
                         QStringLiteral("org.freedesktop.DBus.Properties"),
                         QDBusConnection::systemBus());
    if (iface.isValid()) {
        QDBusReply<QVariant> reply = iface.call(QStringLiteral("Get"), QStringLiteral("net.hadess.PowerProfiles"), QStringLiteral("ActiveProfile"));
        if (reply.isValid()) {
            return reply.value().toString();
        }
    }

    const QString ppctl = QStandardPaths::findExecutable(QStringLiteral("powerprofilesctl"));
    if (!ppctl.isEmpty()) {
        QProcess proc;
        proc.start(ppctl, {QStringLiteral("get")});
        if (proc.waitForFinished(800) && proc.exitCode() == 0) {
            return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        }
    }
    return QString();
}

QString DeviceManager::getActivePowerProfileDescription() const
{
    const QString profile = getActivePowerProfile();
    if (profile == QLatin1String("performance")) {
        return i18n("El perfil de energía actual es Rendimiento (máxima potencia).");
    } else if (profile == QLatin1String("power-saver")) {
        return i18n("El perfil de energía actual es Ahorro de Energía (bajo consumo).");
    } else if (profile == QLatin1String("balanced")) {
        return i18n("El perfil de energía actual es Equilibrado.");
    }
    return i18n("El perfil de energía actual es %1.", profile.isEmpty() ? i18n("desconocido") : profile);
}

bool DeviceManager::setActivePowerProfile(const QString &profile) const
{
    QDBusInterface iface(QStringLiteral("net.hadess.PowerProfiles"),
                         QStringLiteral("/net/hadess/PowerProfiles"),
                         QStringLiteral("org.freedesktop.DBus.Properties"),
                         QDBusConnection::systemBus());
    if (iface.isValid()) {
        QDBusReply<void> reply = iface.call(QStringLiteral("Set"),
                                            QStringLiteral("net.hadess.PowerProfiles"),
                                            QStringLiteral("ActiveProfile"),
                                            QVariant::fromValue(QDBusVariant(profile)));
        if (reply.isValid()) {
            return true;
        }
    }

    const QString ppctl = QStandardPaths::findExecutable(QStringLiteral("powerprofilesctl"));
    if (!ppctl.isEmpty()) {
        return (QProcess::execute(ppctl, {QStringLiteral("set"), profile}) == 0);
    }
    return false;
}

QString DeviceManager::getLocalBatteryInfo() const
{
    QDBusInterface iface(QStringLiteral("org.freedesktop.UPower"),
                         QStringLiteral("/org/freedesktop/UPower/devices/DisplayDevice"),
                         QStringLiteral("org.freedesktop.DBus.Properties"),
                         QDBusConnection::systemBus());
    if (iface.isValid()) {
        QDBusReply<QVariant> pctReply = iface.call(QStringLiteral("Get"), QStringLiteral("org.freedesktop.UPower.Device"), QStringLiteral("Percentage"));
        QDBusReply<QVariant> stateReply = iface.call(QStringLiteral("Get"), QStringLiteral("org.freedesktop.UPower.Device"), QStringLiteral("State"));
        if (pctReply.isValid()) {
            const double pct = pctReply.value().toDouble();
            const int stateVal = stateReply.isValid() ? stateReply.value().toInt() : 0;
            const QString stateStr = (stateVal == 1) ? i18n("cargando") : (stateVal == 2) ? i18n("descargando") : i18n("completa");
            return i18n("La batería está al %1% (%2).", QString::number(pct, 'f', 0), stateStr);
        }
    }
    return QString();
}

int DeviceManager::getBatteryPercentage() const
{
    QDBusInterface iface(QStringLiteral("org.freedesktop.UPower"),
                         QStringLiteral("/org/freedesktop/UPower/devices/DisplayDevice"),
                         QStringLiteral("org.freedesktop.DBus.Properties"),
                         QDBusConnection::systemBus());
    if (iface.isValid()) {
        QDBusReply<QVariant> pctReply = iface.call(QStringLiteral("Get"), QStringLiteral("org.freedesktop.UPower.Device"), QStringLiteral("Percentage"));
        if (pctReply.isValid()) {
            return qRound(pctReply.value().toDouble());
        }
    }
    return -1;
}

bool DeviceManager::isDoNotDisturbActive() const
{
    QDBusInterface iface(QStringLiteral("org.freedesktop.Notifications"),
                         QStringLiteral("/org/freedesktop/Notifications"),
                         QStringLiteral("org.freedesktop.Notifications"),
                         QDBusConnection::sessionBus());
    if (iface.isValid()) {
        QDBusReply<bool> reply = iface.call(QStringLiteral("inhibited"));
        return reply.isValid() ? reply.value() : false;
    }
    return false;
}

bool DeviceManager::setDoNotDisturb(bool active) const
{
    QDBusInterface iface(QStringLiteral("org.freedesktop.Notifications"),
                         QStringLiteral("/org/freedesktop/Notifications"),
                         QStringLiteral("org.freedesktop.Notifications"),
                         QDBusConnection::sessionBus());
    if (iface.isValid()) {
        iface.call(QStringLiteral("Inhibit"), QStringLiteral("Kity"), active ? QStringLiteral("Modo No Molestar") : QStringLiteral(""));
        return true;
    }
    return false;
}

bool DeviceManager::lockScreen() const
{
    return QProcess::startDetached(QStringLiteral("qdbus6"),
        {QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/ScreenSaver"), QStringLiteral("Lock")});
}

} // namespace Kity
