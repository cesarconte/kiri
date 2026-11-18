/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "calendarmanager.h"
#include "timermanager.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <KLocalizedString>
#include <KNotification>

namespace Kity {

QJsonObject CalendarEvent::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("title")] = title;
    obj[QStringLiteral("description")] = description;
    obj[QStringLiteral("start_time")] = startTime.toString(Qt::ISODate);
    obj[QStringLiteral("end_time")] = endTime.toString(Qt::ISODate);
    obj[QStringLiteral("calendar_name")] = calendarName;
    if (!location.isEmpty()) {
        obj[QStringLiteral("location")] = location;
    }
    return obj;
}

QVariantMap CalendarEvent::toCardMap() const
{
    QVariantMap card;
    card[QStringLiteral("title")] = title;
    card[QStringLiteral("startTime")] = startTime.toString(QStringLiteral("HH:mm"));
    card[QStringLiteral("endTime")] = endTime.toString(QStringLiteral("HH:mm"));
    card[QStringLiteral("dateFormatted")] = QLocale(QLocale::Spanish, QLocale::Spain).toString(startTime.date(), QStringLiteral("dddd, d 'de' MMMM"));
    card[QStringLiteral("dayNumber")] = startTime.toString(QStringLiteral("d"));
    card[QStringLiteral("monthShort")] = QLocale(QLocale::Spanish, QLocale::Spain).toString(startTime.date(), QStringLiteral("MMM")).toUpper();
    card[QStringLiteral("description")] = description;
    card[QStringLiteral("calendarName")] = calendarName.isEmpty() ? QStringLiteral("Google Calendar (César Conte)") : calendarName;
    return card;
}

CalendarManager &CalendarManager::instance()
{
    static CalendarManager s_instance;
    return s_instance;
}

CalendarManager::CalendarManager(QObject *parent)
    : QObject(parent)
{
}

CalendarEvent CalendarManager::parsePrompt(const QString &prompt, const QDateTime &now) const
{
    CalendarEvent ev;
    ev.title = prompt.trimmed();
    ev.calendarName = QStringLiteral("César Conte");
    ev.startTime = now.addSecs(3600); // Default: 1 hour from now
    ev.endTime = ev.startTime.addSecs(3600);

    // Matching: "a las 16:00", "a las 4 pm", etc.
    static const QRegularExpression timeRegex(
        QStringLiteral("(?:a las|a la)\\s+(\\d{1,2})(?::(\\d{2}))?\\s*(am|pm)?"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = timeRegex.match(prompt);
    if (m.hasMatch()) {
        int hour = m.captured(1).toInt();
        const int minute = m.captured(2).isEmpty() ? 0 : m.captured(2).toInt();
        const QString ampm = m.captured(3).toLower();
        if (ampm == QStringLiteral("pm") && hour < 12) hour += 12;
        if (ampm == QStringLiteral("am") && hour == 12) hour = 0;

        QDate d = now.date();
        if (prompt.contains(QStringLiteral("mañana"), Qt::CaseInsensitive)) {
            d = d.addDays(1);
        } else if (prompt.contains(QStringLiteral("pasado mañana"), Qt::CaseInsensitive)) {
            d = d.addDays(2);
        }
        ev.startTime = QDateTime(d, QTime(hour, minute));
        ev.endTime = ev.startTime.addSecs(3600);
    }
    return ev;
}

QString CalendarManager::generateIcsFile(const CalendarEvent &event) const
{
    const QString calDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity/calendar");
    QDir().mkpath(calDir);

    const QString eventUid = QStringLiteral("kity_%1").arg(QDateTime::currentMSecsSinceEpoch());
    const QString icsPath = calDir + QStringLiteral("/%1.ics").arg(eventUid);

    QFile icsFile(icsPath);
    if (icsFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&icsFile);
        out << "BEGIN:VCALENDAR\n"
            << "VERSION:2.0\n"
            << "PRODID:-//Kity//KDE Plasma 6.8//ES\n"
            << "X-WR-CALNAME:" << (event.calendarName.isEmpty() ? QStringLiteral("César Conte") : event.calendarName) << "\n"
            << "BEGIN:VEVENT\n"
            << "UID:" << eventUid << "\n"
            << "DTSTAMP:" << QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'hhmmss'Z'")) << "\n"
            << "DTSTART:" << event.startTime.toUTC().toString(QStringLiteral("yyyyMMdd'T'hhmmss'Z'")) << "\n"
            << "DTEND:" << event.endTime.toUTC().toString(QStringLiteral("yyyyMMdd'T'hhmmss'Z'")) << "\n"
            << "SUMMARY:" << event.title << "\n";
        if (!event.description.isEmpty()) {
            out << "DESCRIPTION:" << event.description << "\n";
        }
        if (!event.location.isEmpty()) {
            out << "LOCATION:" << event.location << "\n";
        }
        out << "END:VEVENT\n"
            << "END:VCALENDAR\n";
        icsFile.close();
        return icsPath;
    }

    return QString();
}

bool CalendarManager::createEvent(const CalendarEvent &event, QString *icsPathOut) const
{
    const QString icsPath = generateIcsFile(event);
    if (icsPath.isEmpty()) {
        return false;
    }

    if (icsPathOut) {
        *icsPathOut = icsPath;
    }

    // Merge into KOrganizer (default calendar "César Conte")
    const QString korganizer = QStandardPaths::findExecutable(QStringLiteral("korganizer"));
    if (!korganizer.isEmpty()) {
        QProcess::startDetached(korganizer, QStringList() << QStringLiteral("-m") << icsPath);
    }

    // Synchronize Google Calendar via Akonadi
    syncGoogleCalendar();

    // Register reminder notification
    TimerManager::instance().scheduleReminder(event.title, event.startTime);

    KNotification::event(QStringLiteral("notification"),
                         i18n("Evento agendado en KOrganizer"),
                         QStringLiteral("%1 (%2)").arg(event.title, event.startTime.toString(QStringLiteral("dd/MM/yyyy HH:mm"))),
                         QStringLiteral("x-office-calendar"));

    return true;
}

void CalendarManager::syncGoogleCalendar() const
{
    QDBusInterface agentMgr(QStringLiteral("org.freedesktop.Akonadi.Control"),
                            QStringLiteral("/AgentManager"),
                            QStringLiteral("org.freedesktop.Akonadi.AgentManager"),
                            QDBusConnection::sessionBus());
    if (agentMgr.isValid()) {
        const QDBusReply<QStringList> reply = agentMgr.call(QStringLiteral("agentInstances"));
        if (reply.isValid()) {
            for (const QString &inst : reply.value()) {
                if (inst.startsWith(QStringLiteral("akonadi_google_resource"))
                    || inst.startsWith(QStringLiteral("akonadi_ical_resource"))) {
                    agentMgr.call(QStringLiteral("agentInstanceSynchronize"), inst);
                }
            }
        }
    }
}

} // namespace Kity
