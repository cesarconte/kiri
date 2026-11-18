/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

namespace Kity {

struct ScheduledReminder {
    QString id;
    QString text;
    QDateTime targetTime;
    bool notified = false;
};

class TimerManager : public QObject
{
    Q_OBJECT

public:
    static TimerManager &instance();

    ScheduledReminder addTimer(int seconds, const QString &label);
    ScheduledReminder scheduleReminder(const QString &text, const QDateTime &targetTime);

    QList<ScheduledReminder> listActiveReminders() const;
    QList<ScheduledReminder> allReminders() const;

    void loadReminders();
    void saveReminders();
    void checkReminders();

Q_SIGNALS:
    void timerFinished(int seconds, const QString &label);
    void reminderTriggered(const ScheduledReminder &reminder);

private:
    explicit TimerManager(QObject *parent = nullptr);
    ~TimerManager() override = default;

    TimerManager(const TimerManager &) = delete;
    TimerManager &operator=(const TimerManager &) = delete;

    QString storageFilePath() const;

    QList<ScheduledReminder> m_reminders;
};

} // namespace Kity
