/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>

namespace Kity {

struct EmailDraft {
    QString id;
    QString to;
    QString toName;
    QString subject;
    QString body;
    QString status = QStringLiteral("draft"); // "draft", "awaiting_confirmation", "sent", "discarded"
    QString client = QStringLiteral("proton");
    QDateTime timestamp;

    QJsonObject toJson() const;
    QVariantMap toCardMap() const;
};

/**
 * @brief EmailManager manages email composition through the official Proton Mail desktop application,
 * safe draft workflows with human-in-the-loop confirmation, automatic discard, and Proton Mail Bridge integration.
 */
class EmailManager : public QObject
{
    Q_OBJECT

public:
    static EmailManager &instance();

    bool isProtonMailInstalled() const;
    bool isBridgeRunning() const;
    QUrl buildMailtoUrl(const QString &to, const QString &subject = QString(), const QString &body = QString()) const;

    // Natural language parsing & event creation
    EmailDraft parsePrompt(const QString &prompt) const;

    // Contact resolution (local address book / Akonadi / Google Contacts)
    bool resolveContact(const QString &query, QString *emailOut, QString *nameOut = nullptr) const;

    // Draft lifecycle & Safety Guardrails
    EmailDraft prepareDraft(const QString &to,
                            const QString &subject = QString(),
                            const QString &body = QString(),
                            const QString &preferredClient = QStringLiteral("proton"),
                            const QString &tone = QString());

    bool hasPendingDraft() const;
    EmailDraft currentDraft() const;

    bool sendPendingDraft(QString *errorMsg = nullptr);
    bool discardPendingDraft();
    bool openInProtonDesktop(const EmailDraft &draft) const;
    bool closeProtonMailWindow() const;

    // Direct composition (opens compose window directly in desktop client)
    bool composeEmail(const QString &to,
                      const QString &subject = QString(),
                      const QString &body = QString(),
                      const QString &preferredClient = QStringLiteral("proton")) const;

Q_SIGNALS:
    void draftUpdated(const EmailDraft &draft);
    void draftSent(const EmailDraft &draft);
    void draftDiscarded(const EmailDraft &draft);

private:
    explicit EmailManager(QObject *parent = nullptr);
    ~EmailManager() override = default;

    EmailManager(const EmailManager &) = delete;
    EmailManager &operator=(const EmailManager &) = delete;

    EmailDraft m_pendingDraft;
    bool m_hasPendingDraft = false;
};

} // namespace Kity
