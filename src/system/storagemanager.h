/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QList>

namespace Kity {

struct StoredContact {
    QString name;
    QString phone;
    QString email;
};

struct StoredReminder {
    QString id;
    QString text;
    QDateTime targetTime;
    bool done = false;
};

class StorageManager : public QObject
{
    Q_OBJECT

public:
    static StorageManager &instance();

    // Conversation History
    QVariantList loadHistory() const;
    void saveHistory(const QVariantList &messages);
    void clearHistory();
    QString historyFilePath() const;

    // Contacts
    QList<StoredContact> loadContacts() const;
    void saveContacts(const QList<StoredContact> &contacts);
    void importContactsFromVcf(const QString &vcfPath);
    StoredContact findContactByName(const QString &query) const;
    QString contactsFilePath() const;

    // Scheduled Reminders
    QList<StoredReminder> loadReminders() const;
    void saveReminders(const QList<StoredReminder> &reminders);
    QString remindersFilePath() const;

    static QString normalizeForMatch(const QString &text);

private:
    explicit StorageManager(QObject *parent = nullptr);
    ~StorageManager() override = default;

    StorageManager(const StorageManager &) = delete;
    StorageManager &operator=(const StorageManager &) = delete;
};

} // namespace Kity
