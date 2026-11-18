/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "emailtool.h"
#include "../system/emailmanager.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <KLocalizedString>

namespace Kity {

bool EmailTool::matchesQuery(const QString &query) const
{
    static const QRegularExpression mailSignal(
        QStringLiteral("(correo|email|mail|escribe\\s+a|enviar\\s+correo|mandar\\s+un\\s+correo)"),
        QRegularExpression::CaseInsensitiveOption);
    return query.contains(mailSignal);
}

QJsonObject EmailTool::parameters() const
{
    QJsonObject props;

    QJsonObject actionProp;
    actionProp[QStringLiteral("type")] = QStringLiteral("string");
    actionProp[QStringLiteral("enum")] = QJsonArray({QStringLiteral("prepare"), QStringLiteral("send"), QStringLiteral("discard"), QStringLiteral("open")});
    actionProp[QStringLiteral("description")] = QStringLiteral("Acción a realizar: 'prepare' (redacta borrador y solicita confirmación), 'send' (envía el borrador tras confirmación), 'discard' (cancela y borra el borrador), 'open' (abre directamente en Proton Mail).");
    props[QStringLiteral("action")] = actionProp;

    QJsonObject toProp;
    toProp[QStringLiteral("type")] = QStringLiteral("string");
    toProp[QStringLiteral("description")] = QStringLiteral("Destinatario o nombre de contacto (ej. 'contacto@ejemplo.com' o 'César').");
    props[QStringLiteral("to")] = toProp;

    QJsonObject subjProp;
    subjProp[QStringLiteral("type")] = QStringLiteral("string");
    subjProp[QStringLiteral("description")] = QStringLiteral("Asunto del mensaje de correo.");
    props[QStringLiteral("subject")] = subjProp;

    QJsonObject bodyProp;
    bodyProp[QStringLiteral("type")] = QStringLiteral("string");
    bodyProp[QStringLiteral("description")] = QStringLiteral("Cuerpo o contenido del correo.");
    props[QStringLiteral("body")] = bodyProp;

    QJsonObject toneProp;
    toneProp[QStringLiteral("type")] = QStringLiteral("string");
    toneProp[QStringLiteral("enum")] = QJsonArray({QStringLiteral("formal"), QStringLiteral("cordial"), QStringLiteral("casual"), QStringLiteral("conciso")});
    toneProp[QStringLiteral("description")] = QStringLiteral("Tono o estilo de la redacción.");
    props[QStringLiteral("tone")] = toneProp;

    QJsonObject clientProp;
    clientProp[QStringLiteral("type")] = QStringLiteral("string");
    clientProp[QStringLiteral("description")] = QStringLiteral("Cliente preferente: 'proton' (aplicación oficial Proton Mail) o 'gmail'.");
    props[QStringLiteral("client")] = clientProp;

    QJsonObject params;
    params[QStringLiteral("type")] = QStringLiteral("object");
    params[QStringLiteral("properties")] = props;

    return params;
}

QJsonObject EmailTool::execute(const QJsonObject &arguments)
{
    const QString action = arguments.value(QStringLiteral("action")).toString(QStringLiteral("prepare")).toLower();
    const QString to = arguments.value(QStringLiteral("to")).toString().trimmed();
    const QString subject = arguments.value(QStringLiteral("subject")).toString().trimmed();
    const QString body = arguments.value(QStringLiteral("body")).toString().trimmed();
    const QString tone = arguments.value(QStringLiteral("tone")).toString().trimmed();
    const QString client = arguments.value(QStringLiteral("client")).toString().trimmed();

    QJsonObject result;

    if (action == QStringLiteral("discard")) {
        const bool ok = EmailManager::instance().discardPendingDraft();
        result[QStringLiteral("success")] = ok;
        result[QStringLiteral("action")] = QStringLiteral("discard");
        result[QStringLiteral("status")] = ok
            ? i18n("Borrador de correo descartado y eliminado con éxito.")
            : i18n("No había ningún borrador pendiente para descartar.");
        return result;
    }

    if (action == QStringLiteral("send")) {
        QString err;
        const bool ok = EmailManager::instance().sendPendingDraft(&err);
        result[QStringLiteral("success")] = ok;
        result[QStringLiteral("action")] = QStringLiteral("send");
        result[QStringLiteral("status")] = ok
            ? i18n("Correo enviado con éxito mediante Proton Mail Bridge.")
            : (err.isEmpty() ? i18n("No se pudo enviar el correo.") : err);
        return result;
    }

    if (action == QStringLiteral("open")) {
        const bool ok = EmailManager::instance().composeEmail(to, subject, body, client.isEmpty() ? QStringLiteral("proton") : client);
        result[QStringLiteral("success")] = ok;
        result[QStringLiteral("action")] = QStringLiteral("open");
        result[QStringLiteral("status")] = ok
            ? i18n("Aplicación oficial de Proton Mail abierta con el mensaje.")
            : i18n("No se pudo abrir Proton Mail.");
        return result;
    }

    // Default: 'prepare' draft with Safety Guardrail (Human-in-the-loop)
    const EmailDraft draft = EmailManager::instance().prepareDraft(to, subject, body, client.isEmpty() ? QStringLiteral("proton") : client, tone);
    result[QStringLiteral("success")] = true;
    result[QStringLiteral("action")] = QStringLiteral("prepare");
    result[QStringLiteral("draft")] = draft.toJson();
    result[QStringLiteral("requires_confirmation")] = true;
    result[QStringLiteral("status")] = i18n("Borrador preparado para %1 con asunto '%2'. Esperando confirmación del usuario para enviarlo o descartarlo.",
                                            draft.toName.isEmpty() ? draft.to : draft.toName, draft.subject);
    return result;
}

} // namespace Kity
