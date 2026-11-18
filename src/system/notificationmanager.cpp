/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "notificationmanager.h"
#include "timermanager.h"

#include <QDateTime>
#include <QProcess>
#include <QRegularExpression>
#include <KLocalizedString>
#include <KNotification>

namespace Kity {

QJsonObject NotificationItem::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("sender")] = sender;
    obj[QStringLiteral("summary")] = summary;
    obj[QStringLiteral("body")] = body;
    obj[QStringLiteral("timestamp")] = timestamp.toString(Qt::ISODate);
    obj[QStringLiteral("urgency")] = urgency;
    return obj;
}

NotificationManager &NotificationManager::instance()
{
    static NotificationManager s_instance;
    return s_instance;
}

NotificationManager::NotificationManager(QObject *parent)
    : QObject(parent)
{
}

QList<NotificationItem> NotificationManager::recentNotifications(int maxHours) const
{
    QList<NotificationItem> items;
    const QString sinceArg = QStringLiteral("%1 hours ago").arg(maxHours <= 0 ? 2 : maxHours);

    // Query journalctl for system warnings/errors/notices
    QProcess proc;
    proc.start(QStringLiteral("journalctl"), {
        QStringLiteral("--user"),
        QStringLiteral("-p"), QStringLiteral("4..0"),
        QStringLiteral("--since"), sinceArg,
        QStringLiteral("-n"), QStringLiteral("15"),
        QStringLiteral("--no-pager"),
        QStringLiteral("-o"), QStringLiteral("short-iso")
    });

    if (proc.waitForFinished(1000)) {
        const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        if (!out.isEmpty()) {
            const QStringList rawLines = out.split(QLatin1Char('\n'));
            for (const QString &line : rawLines) {
                const QString trimmed = line.trimmed();
                if (trimmed.isEmpty() || trimmed.contains(QLatin1String("Unsupported return type"))) {
                    continue;
                }

                NotificationItem item;
                // Parse ISO date at beginning: 2026-11-18T10:15:00+0100 hostname proc[pid]: message
                static const QRegularExpression logRegex(QStringLiteral("^(\\d{4}-\\d{2}-\\d{2}T[^\\s]+)\\s+[^\\s]+\\s+([^:\\[]+)(?:\\[\\d+\\])?:\\s*(.*)$"));
                const auto m = logRegex.match(trimmed);
                if (m.hasMatch()) {
                    item.timestamp = QDateTime::fromString(m.captured(1), Qt::ISODate);
                    item.sender = m.captured(2).trimmed();
                    item.body = m.captured(3).trimmed().left(200);
                    item.summary = item.sender;
                } else {
                    item.sender = QStringLiteral("Sistema");
                    item.summary = QStringLiteral("Aviso del sistema");
                    item.body = trimmed.left(200);
                    item.timestamp = QDateTime::currentDateTime();
                }

                if (item.body.contains(QStringLiteral("error"), Qt::CaseInsensitive)
                    || item.body.contains(QStringLiteral("fail"), Qt::CaseInsensitive)
                    || item.body.contains(QStringLiteral("critical"), Qt::CaseInsensitive)) {
                    item.urgency = QStringLiteral("critical");
                } else {
                    item.urgency = QStringLiteral("normal");
                }

                items.append(item);
            }
        }
    }

    return items;
}

QString NotificationManager::collectRecentNotificationsContext(int maxHours) const
{
    QStringList lines;

    const QList<NotificationItem> notifs = recentNotifications(maxHours);
    if (!notifs.isEmpty()) {
        lines << QStringLiteral("--- Avisos y Eventos del Sistema (últimas %1 horas) ---").arg(maxHours <= 0 ? 2 : maxHours);
        for (const auto &item : notifs) {
            lines << QStringLiteral("• [%1] %2: %3").arg(item.urgency.toUpper(), item.sender, item.body);
        }
    }

    // Reminders & Timers
    const QDateTime now = QDateTime::currentDateTime();
    int pendingReminders = 0;
    for (const auto &r : TimerManager::instance().allReminders()) {
        if (r.targetTime >= now && r.targetTime <= now.addSecs(86400)) {
            if (pendingReminders == 0) {
                lines << QStringLiteral("\n--- Recordatorios y Temporizadores programados hoy ---");
            }
            lines << QStringLiteral("• %1 a las %2").arg(r.text, r.targetTime.toString(QStringLiteral("HH:mm")));
            pendingReminders++;
        }
    }

    if (lines.isEmpty()) {
        return QStringLiteral("No hay notificaciones ni avisos pendientes en el sistema en las últimas %1 horas.").arg(maxHours <= 0 ? 2 : maxHours);
    }

    return lines.join(QLatin1Char('\n'));
}

QString NotificationManager::summarizeNotifications(const QString &filter) const
{
    Q_UNUSED(filter);
    return collectRecentNotificationsContext(2);
}

bool NotificationManager::sendNotification(const QString &title,
                                           const QString &text,
                                           const QString &icon,
                                           const QString &urgency) const
{
    Q_UNUSED(urgency);
    KNotification::event(QStringLiteral("notification"),
                         title,
                         text,
                         icon.isEmpty() ? QStringLiteral("dialog-information") : icon);
    return true;
}

} // namespace Kity
