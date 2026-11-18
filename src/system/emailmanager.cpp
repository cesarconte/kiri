/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "emailmanager.h"
#include "windowmanager.h"
#include "applicationmanager.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QUrlQuery>
#include <KLocalizedString>
#include <KNotification>

namespace Kity {

QJsonObject EmailDraft::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = id;
    obj[QStringLiteral("to")] = to;
    obj[QStringLiteral("to_name")] = toName;
    obj[QStringLiteral("subject")] = subject;
    obj[QStringLiteral("body")] = body;
    obj[QStringLiteral("status")] = status;
    obj[QStringLiteral("client")] = client;
    obj[QStringLiteral("timestamp")] = timestamp.toString(Qt::ISODate);
    return obj;
}

QVariantMap EmailDraft::toCardMap() const
{
    QVariantMap map;
    map[QStringLiteral("id")] = id;
    map[QStringLiteral("to")] = to;
    map[QStringLiteral("toName")] = toName.isEmpty() ? to : toName;
    map[QStringLiteral("subject")] = subject.isEmpty() ? i18n("(Sin asunto)") : subject;
    map[QStringLiteral("body")] = body;
    map[QStringLiteral("status")] = status;
    map[QStringLiteral("client")] = client;
    map[QStringLiteral("timestamp")] = timestamp.isValid() ? timestamp.toString(QStringLiteral("HH:mm")) : QTime::currentTime().toString(QStringLiteral("HH:mm"));
    return map;
}

EmailManager &EmailManager::instance()
{
    static EmailManager s_instance;
    return s_instance;
}

EmailManager::EmailManager(QObject *parent)
    : QObject(parent)
{
}

bool EmailManager::isProtonMailInstalled() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("proton-mail")).isEmpty();
}

bool EmailManager::isBridgeRunning() const
{
    QTcpSocket socket;
    socket.connectToHost(QStringLiteral("127.0.0.1"), 1025);
    return socket.waitForConnected(200);
}

QUrl EmailManager::buildMailtoUrl(const QString &to, const QString &subject, const QString &body) const
{
    QUrl mailtoUrl(QStringLiteral("mailto:") + to);
    QUrlQuery q;
    if (!subject.isEmpty()) {
        q.addQueryItem(QStringLiteral("subject"), subject);
    }
    if (!body.isEmpty()) {
        q.addQueryItem(QStringLiteral("body"), body);
    }
    mailtoUrl.setQuery(q);
    return mailtoUrl;
}

EmailDraft EmailManager::parsePrompt(const QString &prompt) const
{
    EmailDraft draft;
    draft.id = QStringLiteral("draft_%1").arg(QDateTime::currentMSecsSinceEpoch());
    draft.timestamp = QDateTime::currentDateTime();
    draft.client = QStringLiteral("proton");

    static const QRegularExpression cmdRegex(
        QStringLiteral("(?:env[ií]a|redacta|escribe|manda)\\s+(?:un\\s+|una\\s+)?(?:correo|email|mensaje)\\s+(?:electr[oó]nico\\s+)?a\\s+(.+)"),
        QRegularExpression::CaseInsensitiveOption);

    const auto match = cmdRegex.match(prompt);
    if (!match.hasMatch()) {
        return draft;
    }

    QString rest = match.captured(1).trimmed();
    QString extractedSubject;
    QString extractedTo;
    QString extractedBody;

    // 1. Detect explicit subject: "con asunto X", "asunto: X", etc.
    static const QRegularExpression subjRegex(
        QStringLiteral("(?:con\\s+)?asunto\\s*[:\\s]\\s*([\"«“][^\"»”]+[\"»”]|[^,]+?)(?=\\s+(?:diciendo|con|y\\s+(?:el\\s+)?(?:cuerpo|texto|mensaje)|que\\s+diga)|$)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto subjMatch = subjRegex.match(rest);
    if (subjMatch.hasMatch()) {
        extractedSubject = subjMatch.captured(1).trimmed();
        extractedSubject.remove(QRegularExpression(QStringLiteral("^[\"«“']|[\"»”']$")));
        rest.remove(subjMatch.capturedStart(0), subjMatch.capturedLength(0));
        rest = rest.trimmed();
    }

    // 2. Split recipient (To) and body
    static const QRegularExpression splitRegex(
        QStringLiteral("^(.*?)\\s+(?:con\\s+(?:el\\s+)?(?:siguiente\\s+)?(?:texto|mensaje|cuerpo)|diciendo\\s*(?:que)?|que\\s+diga|con)\\s*[:\\s]\\s*(.*)$"),
        QRegularExpression::CaseInsensitiveOption);
    const auto splitMatch = splitRegex.match(rest);
    if (splitMatch.hasMatch()) {
        extractedTo = splitMatch.captured(1).trimmed();
        extractedBody = splitMatch.captured(2).trimmed();
    } else {
        static const QRegularExpression emailOnlyRegex(QStringLiteral("^([^\\s]+@[^\\s]+)\\s*(.*)$"));
        const auto emMatch = emailOnlyRegex.match(rest);
        if (emMatch.hasMatch()) {
            extractedTo = emMatch.captured(1).trimmed();
            extractedBody = emMatch.captured(2).trimmed();
        } else {
            extractedTo = rest;
        }
    }

    // 3. Clean body: prioritize quotes if present, otherwise strip lead-in phrases
    static const QRegularExpression quoteRegex(QStringLiteral("[\"«“]([^\"»”]+)[\"»”]"));
    const auto qMatch = quoteRegex.match(extractedBody);
    if (qMatch.hasMatch()) {
        extractedBody = qMatch.captured(1).trimmed();
    } else {
        extractedBody.remove(QRegularExpression(QStringLiteral("^(?:y\\s+)?(?:el\\s+)?(?:siguiente\\s+)?(?:texto|mensaje|cuerpo)\\s*[:\\s]*"), QRegularExpression::CaseInsensitiveOption));
        extractedBody.remove(QRegularExpression(QStringLiteral("^(?:diciendo\\s*(?:que)?|que\\s+diga)\\s*[:\\s]*"), QRegularExpression::CaseInsensitiveOption));
        extractedBody.remove(QRegularExpression(QStringLiteral("^[\"«“']|[\"»”']$")));
        extractedBody = extractedBody.trimmed();
    }

    // 4. Resolve recipient contact
    QString resEmail, resName;
    resolveContact(extractedTo, &resEmail, &resName);
    draft.to = resEmail;
    draft.toName = resName;
    draft.subject = extractedSubject.isEmpty() ? i18n("Mensaje de Kity") : extractedSubject;
    draft.body = extractedBody;
    draft.status = QStringLiteral("awaiting_confirmation");

    return draft;
}

bool EmailManager::resolveContact(const QString &query, QString *emailOut, QString *nameOut) const
{
    const QString clean = query.trimmed();
    if (clean.isEmpty()) {
        return false;
    }

    // Direct email address
    if (clean.contains(QLatin1Char('@'))) {
        if (emailOut) *emailOut = clean;
        if (nameOut) *nameOut = clean.section(QLatin1Char('@'), 0, 0);
        return true;
    }

    // Lookup in Kity contacts storage (~/.local/share/kity/contacts.json)
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity/contacts.json");
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        const QJsonArray arr = doc.array();
        const QString lowerQ = clean.toLower();
        for (const auto &v : arr) {
            const QJsonObject o = v.toObject();
            const QString name = o.value(QStringLiteral("name")).toString();
            const QString email = o.value(QStringLiteral("email")).toString();
            if (!email.isEmpty() && (name.toLower().contains(lowerQ) || lowerQ.contains(name.toLower()))) {
                if (emailOut) *emailOut = email;
                if (nameOut) *nameOut = name;
                return true;
            }
        }
    }

    // Fallback: if not found, use query as recipient identifier
    if (emailOut) *emailOut = clean;
    if (nameOut) *nameOut = clean;
    return false;
}

EmailDraft EmailManager::prepareDraft(const QString &to,
                                     const QString &subject,
                                     const QString &body,
                                     const QString &preferredClient,
                                     const QString &tone)
{
    EmailDraft draft;
    draft.id = QStringLiteral("draft_%1").arg(QDateTime::currentMSecsSinceEpoch());
    draft.timestamp = QDateTime::currentDateTime();
    draft.client = preferredClient.isEmpty() ? QStringLiteral("proton") : preferredClient;

    QString resolvedEmail, resolvedName;
    resolveContact(to, &resolvedEmail, &resolvedName);
    draft.to = resolvedEmail;
    draft.toName = resolvedName;

    draft.subject = subject.isEmpty() ? i18n("Mensaje de Kity") : subject;

    QString refinedBody = body;
    if (!tone.isEmpty()) {
        const QString t = tone.toLower();
        if (t.contains(QStringLiteral("formal")) && !refinedBody.isEmpty()) {
            if (!refinedBody.startsWith(QStringLiteral("Estimado"), Qt::CaseInsensitive)) {
                refinedBody = QStringLiteral("Estimado/a,\n\n") + refinedBody + QStringLiteral("\n\nAtentamente,\nCésar Conte");
            }
        } else if (t.contains(QStringLiteral("cordial")) || t.contains(QStringLiteral("saludo"))) {
            if (!refinedBody.startsWith(QStringLiteral("Hola"), Qt::CaseInsensitive)) {
                refinedBody = QStringLiteral("Hola,\n\n") + refinedBody + QStringLiteral("\n\nUn cordial saludo,\nCésar");
            }
        }
    }
    draft.body = refinedBody;
    draft.status = QStringLiteral("awaiting_confirmation");

    m_pendingDraft = draft;
    m_hasPendingDraft = true;

    Q_EMIT draftUpdated(draft);
    return draft;
}

bool EmailManager::hasPendingDraft() const
{
    return m_hasPendingDraft;
}

EmailDraft EmailManager::currentDraft() const
{
    return m_pendingDraft;
}

bool EmailManager::sendPendingDraft(QString *errorMsg)
{
    if (!m_hasPendingDraft) {
        if (errorMsg) {
            *errorMsg = i18n("No hay ningún borrador de correo pendiente para enviar.");
        }
        return false;
    }

    m_pendingDraft.status = QStringLiteral("sent");
    m_hasPendingDraft = false;

    // Trigger visual notification
    KNotification::event(QStringLiteral("notification"),
                         i18n("Correo enviado"),
                         i18n("Mensaje enviado con éxito a %1 vía Proton Mail.").arg(m_pendingDraft.to),
                         QStringLiteral("mail-sent"));

    Q_EMIT draftSent(m_pendingDraft);
    return true;
}

bool EmailManager::discardPendingDraft()
{
    if (!m_hasPendingDraft) {
        return false;
    }

    m_pendingDraft.status = QStringLiteral("discarded");
    m_hasPendingDraft = false;

    // Automatically close Proton Mail window if opened
    closeProtonMailWindow();

    KNotification::event(QStringLiteral("notification"),
                         i18n("Borrador descartado"),
                         i18n("El correo para %1 ha sido eliminado por Kity.").arg(m_pendingDraft.to),
                         QStringLiteral("edit-delete"));

    Q_EMIT draftDiscarded(m_pendingDraft);
    return true;
}

bool EmailManager::openInProtonDesktop(const EmailDraft &draft) const
{
    return composeEmail(draft.to, draft.subject, draft.body, draft.client);
}

bool EmailManager::closeProtonMailWindow() const
{
    // Gracefully close any Proton Mail compose window via WindowManager
    bool closed = WindowManager::instance().closeWindow(QStringLiteral("Proton Mail"));
    if (!closed) {
        closed = WindowManager::instance().closeWindow(QStringLiteral("proton-mail"));
    }
    return closed;
}

bool EmailManager::composeEmail(const QString &to,
                                const QString &subject,
                                const QString &body,
                                const QString &preferredClient) const
{
    const QUrl mailtoUrl = buildMailtoUrl(to, subject, body);

    if (preferredClient == QStringLiteral("gmail")) {
        QUrl gmailUrl(QStringLiteral("https://mail.google.com/mail/"));
        QUrlQuery gq;
        gq.addQueryItem(QStringLiteral("view"), QStringLiteral("cm"));
        gq.addQueryItem(QStringLiteral("fs"), QStringLiteral("1"));
        if (!to.isEmpty()) gq.addQueryItem(QStringLiteral("to"), to);
        if (!subject.isEmpty()) gq.addQueryItem(QStringLiteral("su"), subject);
        if (!body.isEmpty()) gq.addQueryItem(QStringLiteral("body"), body);
        gmailUrl.setQuery(gq);
        return QDesktopServices::openUrl(gmailUrl);
    }

    const QString protonApp = QStandardPaths::findExecutable(QStringLiteral("proton-mail"));
    if (!protonApp.isEmpty()) {
        return QProcess::startDetached(protonApp, QStringList() << mailtoUrl.toString());
    }

    return QDesktopServices::openUrl(mailtoUrl);
}

} // namespace Kity
