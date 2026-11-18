/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace Kity {

/**
 * @brief ConversationManager manages disk persistence, retention policies,
 * file attachment ingestion (PDF/OCR), and CRUD operations for conversational history.
 */
class ConversationManager : public QObject
{
    Q_OBJECT

public:
    explicit ConversationManager(QObject *parent = nullptr);

    QVariantList loadConversations(const QString &historyRetention, const QString &openToMode, QString &activeIdOut, QVariantList &activeMessagesOut);
    void saveConversations(const QVariantList &conversations);

    void syncActiveFromMessages(QVariantList &conversations, const QString &activeId, const QVariantList &messages);
    QString createNewConversation(QVariantList &conversations);
    bool selectConversation(const QVariantList &conversations, const QString &id, QVariantList &messagesOut);
    void togglePin(QVariantList &conversations, const QString &id);
    void rename(QVariantList &conversations, const QString &id, const QString &newTitle);
    void deleteConversations(QVariantList &conversations, const QStringList &ids, QString &activeIdInOut, QVariantList &activeMessagesOut);

    QString processAttachedFiles(const QStringList &fileUrls);
};

} // namespace Kity
