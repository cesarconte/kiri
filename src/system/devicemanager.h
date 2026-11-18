/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>

namespace Kity {

class DeviceManager : public QObject
{
    Q_OBJECT

public:
    static DeviceManager &instance();

    // Power Profiles (net.hadess.PowerProfiles / powerprofilesctl)
    QString getActivePowerProfile() const;
    QString getActivePowerProfileDescription() const;
    bool setActivePowerProfile(const QString &profile) const;

    // Battery (org.freedesktop.UPower)
    QString getLocalBatteryInfo() const;
    int getBatteryPercentage() const;

    // Do Not Disturb (org.freedesktop.Notifications inhibition)
    bool isDoNotDisturbActive() const;
    bool setDoNotDisturb(bool active) const;

    // Screen Lock (org.freedesktop.ScreenSaver)
    bool lockScreen() const;

private:
    explicit DeviceManager(QObject *parent = nullptr);
    ~DeviceManager() override = default;

    DeviceManager(const DeviceManager &) = delete;
    DeviceManager &operator=(const DeviceManager &) = delete;
};

} // namespace Kity
