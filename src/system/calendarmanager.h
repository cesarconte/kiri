/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace Kity {

struct CalendarEvent {
    QString title;
    QString description;
    QDateTime startTime;
    QDateTime endTime;
    QString calendarName = QStringLiteral("César Conte");
    QString location;

    QJsonObject toJson() const;
    QVariantMap toCardMap() const;
};

/**
 * @brief CalendarManager manages RFC 5545 iCalendar (.ics) generation,
 * integration with KOrganizer, and triggering synchronization with Akonadi Google Calendar resources.
 */
class CalendarManager : public QObject
{
    Q_OBJECT

public:
    static CalendarManager &instance();

    // Natural language parsing & event creation
    CalendarEvent parsePrompt(const QString &prompt, const QDateTime &now = QDateTime::currentDateTime()) const;
    QString generateIcsFile(const CalendarEvent &event) const;
    bool createEvent(const CalendarEvent &event, QString *icsPathOut = nullptr) const;

    // Synchronize Google Calendar via Akonadi D-Bus
    void syncGoogleCalendar() const;

private:
    explicit CalendarManager(QObject *parent = nullptr);
    ~CalendarManager() override = default;

    CalendarManager(const CalendarManager &) = delete;
    CalendarManager &operator=(const CalendarManager &) = delete;
};

} // namespace Kity
