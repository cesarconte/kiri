/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "storagemanager.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

namespace Kity {

StorageManager &StorageManager::instance()
{
    static StorageManager s_instance;
    return s_instance;
}

StorageManager::StorageManager(QObject *parent)
    : QObject(parent)
{
}

QString StorageManager::historyFilePath() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base);
    return base + QStringLiteral("/history.json");
}

QVariantList StorageManager::loadHistory() const
{
    QVariantList messages;
    QFile file(historyFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return messages;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray()) {
        return messages;
    }

    const QJsonArray arr = doc.array();
    for (const QJsonValue &v : arr) {
        messages.append(v.toObject().toVariantMap());
    }
    return messages;
}

void StorageManager::saveHistory(const QVariantList &messages)
{
    QFile file(historyFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }

    QJsonArray arr;
    const int startIdx = qMax(0, messages.size() - 100);
    for (int i = startIdx; i < messages.size(); ++i) {
        arr.append(QJsonObject::fromVariantMap(messages.at(i).toMap()));
    }

    file.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void StorageManager::clearHistory()
{
    QFile::remove(historyFilePath());
}

QString StorageManager::contactsFilePath() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base);
    return base + QStringLiteral("/contacts.json");
}

QList<StoredContact> StorageManager::loadContacts() const
{
    QList<StoredContact> list;
    QFile file(contactsFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return list;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray()) {
        return list;
    }

    for (const QJsonValue &v : doc.array()) {
        const QJsonObject obj = v.toObject();
        StoredContact c;
        c.name = obj.value(QStringLiteral("name")).toString();
        c.phone = obj.value(QStringLiteral("phone")).toString();
        c.email = obj.value(QStringLiteral("email")).toString();
        if (!c.name.isEmpty()) {
            list.append(c);
        }
    }
    return list;
}

void StorageManager::saveContacts(const QList<StoredContact> &contacts)
{
    QFile file(contactsFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }

    QJsonArray arr;
    for (const auto &c : contacts) {
        QJsonObject obj;
        obj[QStringLiteral("name")] = c.name;
        obj[QStringLiteral("phone")] = c.phone;
        obj[QStringLiteral("email")] = c.email;
        arr.append(obj);
    }
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void StorageManager::importContactsFromVcf(const QString &vcfPath)
{
    QFile f(vcfPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    auto list = loadContacts();
    StoredContact cur;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith(QStringLiteral("BEGIN:VCARD"))) {
            cur = StoredContact();
        } else if (line.startsWith(QStringLiteral("FN:"))) {
            cur.name = line.mid(3).trimmed();
        } else if (line.startsWith(QStringLiteral("TEL;")) || line.startsWith(QStringLiteral("TEL:"))) {
            cur.phone = line.section(QLatin1Char(':'), 1).trimmed();
        } else if (line.startsWith(QStringLiteral("EMAIL;")) || line.startsWith(QStringLiteral("EMAIL:"))) {
            cur.email = line.section(QLatin1Char(':'), 1).trimmed();
        } else if (line.startsWith(QStringLiteral("END:VCARD"))) {
            if (!cur.name.isEmpty()) {
                list.append(cur);
            }
        }
    }
    saveContacts(list);
}

StoredContact StorageManager::findContactByName(const QString &query) const
{
    const QString q = normalizeForMatch(query);
    const auto list = loadContacts();
    for (const auto &c : list) {
        if (normalizeForMatch(c.name).contains(q)) {
            return c;
        }
    }
    return StoredContact();
}

QString StorageManager::remindersFilePath() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base);
    return base + QStringLiteral("/reminders.json");
}

QList<StoredReminder> StorageManager::loadReminders() const
{
    QList<StoredReminder> list;
    QFile file(remindersFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return list;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray()) {
        return list;
    }

    for (const QJsonValue &v : doc.array()) {
        const QJsonObject obj = v.toObject();
        StoredReminder r;
        r.id = obj.value(QStringLiteral("id")).toString();
        r.text = obj.value(QStringLiteral("text")).toString();
        r.targetTime = QDateTime::fromString(obj.value(QStringLiteral("targetTime")).toString(), Qt::ISODate);
        r.done = obj.value(QStringLiteral("done")).toBool();
        if (!r.text.isEmpty() && r.targetTime.isValid()) {
            list.append(r);
        }
    }
    return list;
}

void StorageManager::saveReminders(const QList<StoredReminder> &reminders)
{
    QFile file(remindersFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }

    QJsonArray arr;
    for (const auto &r : reminders) {
        QJsonObject obj;
        obj[QStringLiteral("id")] = r.id;
        obj[QStringLiteral("text")] = r.text;
        obj[QStringLiteral("targetTime")] = r.targetTime.toString(Qt::ISODate);
        obj[QStringLiteral("done")] = r.done;
        arr.append(obj);
    }
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString StorageManager::normalizeForMatch(const QString &text)
{
    return text.toLower().normalized(QString::NormalizationForm_D).remove(QRegularExpression(QStringLiteral("[\\p{M}]")));
}

} // namespace Kity
