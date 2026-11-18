/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "timermanager.h"

#include <KLocalizedString>
#include <KNotification>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

namespace Kity {

TimerManager &TimerManager::instance()
{
    static TimerManager s_instance;
    return s_instance;
}

TimerManager::TimerManager(QObject *parent)
    : QObject(parent)
{
    loadReminders();

    // Check reminders every 15 seconds
    auto timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &TimerManager::checkReminders);
    timer->start(15000);
}

QString TimerManager::storageFilePath() const
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity");
    QDir().mkpath(dataDir);
    return dataDir + QStringLiteral("/reminders.json");
}

void TimerManager::loadReminders()
{
    QFile f(storageFilePath());
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonArray arr = doc.array();
    m_reminders.clear();
    for (const auto &v : arr) {
        const QJsonObject o = v.toObject();
        ScheduledReminder r;
        r.id = o.value(QStringLiteral("id")).toString();
        r.text = o.value(QStringLiteral("text")).toString();
        r.targetTime = QDateTime::fromString(o.value(QStringLiteral("targetTime")).toString(), Qt::ISODate);
        r.notified = o.value(QStringLiteral("notified")).toBool();
        m_reminders.append(r);
    }
}

void TimerManager::saveReminders()
{
    QFile f(storageFilePath());
    if (!f.open(QIODevice::WriteOnly)) {
        return;
    }

    QJsonArray arr;
    for (const auto &r : m_reminders) {
        QJsonObject o;
        o[QStringLiteral("id")] = r.id;
        o[QStringLiteral("text")] = r.text;
        o[QStringLiteral("targetTime")] = r.targetTime.toString(Qt::ISODate);
        o[QStringLiteral("notified")] = r.notified;
        arr.append(o);
    }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

ScheduledReminder TimerManager::addTimer(int seconds, const QString &label)
{
    const QString cleanLabel = label.trimmed().isEmpty() ? i18n("Temporizador") : label.trimmed();

    ScheduledReminder rem;
    rem.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    rem.text = cleanLabel;
    rem.targetTime = QDateTime::currentDateTime().addSecs(seconds);
    rem.notified = false;

    m_reminders.append(rem);
    saveReminders();

    QTimer::singleShot(seconds * 1000, this, [this, seconds, cleanLabel, rem]() {
        KNotification::event(QStringLiteral("notification"),
                             i18n("¡Temporizador de Kity finalizado!"),
                             cleanLabel);
        Q_EMIT timerFinished(seconds, cleanLabel);
        Q_EMIT reminderTriggered(rem);
    });

    return rem;
}

ScheduledReminder TimerManager::scheduleReminder(const QString &text, const QDateTime &targetTime)
{
    ScheduledReminder rem;
    rem.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    rem.text = text.trimmed();
    rem.targetTime = targetTime;
    rem.notified = false;

    m_reminders.append(rem);
    saveReminders();

    return rem;
}

QList<ScheduledReminder> TimerManager::listActiveReminders() const
{
    const QDateTime now = QDateTime::currentDateTime();
    QList<ScheduledReminder> active;
    for (const auto &r : m_reminders) {
        if (!r.notified && r.targetTime >= now) {
            active.append(r);
        }
    }
    return active;
}

QList<ScheduledReminder> TimerManager::allReminders() const
{
    return m_reminders;
}

void TimerManager::checkReminders()
{
    const QDateTime now = QDateTime::currentDateTime();
    bool changed = false;
    for (auto &r : m_reminders) {
        if (!r.notified && r.targetTime.isValid() && r.targetTime <= now) {
            r.notified = true;
            changed = true;

            KNotification::event(QStringLiteral("notification"),
                                 i18n("Recordatorio Kity"),
                                 r.text);
            Q_EMIT reminderTriggered(r);
        }
    }
    if (changed) {
        saveReminders();
    }
}

} // namespace Kity
