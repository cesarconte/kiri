/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>

namespace Kity {

struct KdeConnectDeviceInfo {
    QString id;
    QString name;
};

class KdeConnectManager : public QObject
{
    Q_OBJECT

public:
    static KdeConnectManager &instance();

    QList<KdeConnectDeviceInfo> getDevices() const;
    QString getBatteryInfo(const QString &deviceId) const;
    bool ringDevice(const QString &deviceId) const;
    bool sendSms(const QString &deviceId, const QString &phoneNumber, const QString &message) const;
    bool sendClipboard(const QString &deviceId, const QString &text) const;
    bool initiateCall(const QString &deviceId, const QString &phoneNumber) const;
    bool triggerSmartDucking(bool duck) const;

    static QString normalizePhoneNumber(const QString &raw);
    static QString sanitizePhoneForDial(const QString &raw);
    static bool isRestrictedPhoneNumber(const QString &phone);

private:
    explicit KdeConnectManager(QObject *parent = nullptr);
    ~KdeConnectManager() override = default;

    KdeConnectManager(const KdeConnectManager &) = delete;
    KdeConnectManager &operator=(const KdeConnectManager &) = delete;
};

} // namespace Kity
