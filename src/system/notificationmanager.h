/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

namespace Kity {

struct NotificationItem {
    QString sender;
    QString summary;
    QString body;
    QDateTime timestamp;
    QString urgency = QStringLiteral("normal"); // "low", "normal", "critical"

    QJsonObject toJson() const;
};

/**
 * @brief NotificationManager manages system notification gathering,
 * Apple Intelligence style priority summaries, and notification dispatching.
 */
class NotificationManager : public QObject
{
    Q_OBJECT

public:
    static NotificationManager &instance();

    // Query and priority summaries
    QList<NotificationItem> recentNotifications(int maxHours = 2) const;
    QString collectRecentNotificationsContext(int maxHours = 2) const;
    QString summarizeNotifications(const QString &filter = QString()) const;

    // Sending notifications
    bool sendNotification(const QString &title,
                          const QString &text,
                          const QString &icon = QStringLiteral("dialog-information"),
                          const QString &urgency = QStringLiteral("normal")) const;

private:
    explicit NotificationManager(QObject *parent = nullptr);
    ~NotificationManager() override = default;

    NotificationManager(const NotificationManager &) = delete;
    NotificationManager &operator=(const NotificationManager &) = delete;
};

} // namespace Kity
