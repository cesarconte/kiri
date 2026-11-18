/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ollamaclient.h"

#include <QDebug>
#include <QJsonDocument>
#include <QNetworkRequest>

namespace Kity {

OllamaClient::OllamaClient(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_nam(nam)
{
}

OllamaClient::~OllamaClient()
{
    abort();
}

bool OllamaClient::isBusy() const
{
    return m_currentReply && m_currentReply->isRunning();
}

void OllamaClient::abort()
{
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    m_streamingText.clear();
    m_pendingToolCalls = QJsonArray();
}

void OllamaClient::sendChat(const QUrl &chatUrl, const QJsonObject &reqPayload, int timeoutMs)
{
    abort();

    QNetworkRequest req(chatUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(timeoutMs);

    m_currentReply = m_nam->post(req, QJsonDocument(reqPayload).toJson(QJsonDocument::Compact));
    connect(m_currentReply, &QNetworkReply::readyRead, this, &OllamaClient::onStreamChunk);
    connect(m_currentReply, &QNetworkReply::finished, this, &OllamaClient::onReplyFinished);
}

void OllamaClient::refreshModels(const QUrl &tagsUrl)
{
    QNetworkRequest req(tagsUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Kity/1.0"));
    req.setTransferTimeout(4000);

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const bool reachable = (reply->error() == QNetworkReply::NoError);
        QStringList models;
        if (reachable) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            const QJsonArray modelsArr = doc.object().value(QStringLiteral("models")).toArray();
            for (const QJsonValue &v : modelsArr) {
                const QString mName = v.toObject().value(QStringLiteral("name")).toString();
                if (!mName.isEmpty()) models.append(mName);
            }
        }
        Q_EMIT modelsRefreshed(models, reachable);
    });
}

void OllamaClient::onStreamChunk()
{
    if (!m_currentReply) return;

    while (m_currentReply->canReadLine()) {
        const QByteArray line = m_currentReply->readLine().trimmed();
        if (line.isEmpty()) continue;

        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) continue;

        const QJsonObject obj = doc.object();
        if (obj.contains(QStringLiteral("error"))) {
            const QString err = obj.value(QStringLiteral("error")).toString();
            Q_EMIT errorOccurred(QStringLiteral("Error de Ollama: %1").arg(err), false);
            return;
        }

        const QJsonObject msgObj = obj.value(QStringLiteral("message")).toObject();

        if (msgObj.contains(QStringLiteral("tool_calls"))) {
            const QJsonArray tCalls = msgObj.value(QStringLiteral("tool_calls")).toArray();
            for (const QJsonValue &tcVal : tCalls) {
                const QJsonObject tc = tcVal.toObject();
                const QString tcId = tc.value(QStringLiteral("id")).toString();
                bool exists = false;
                for (const QJsonValue &existing : std::as_const(m_pendingToolCalls)) {
                    if (!tcId.isEmpty() && existing.toObject().value(QStringLiteral("id")).toString() == tcId) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    m_pendingToolCalls.append(tc);
                }
            }
        }

        const QString chunk = msgObj.value(QStringLiteral("content")).toString();
        if (!chunk.isEmpty()) {
            m_streamingText.append(chunk);
            Q_EMIT streamingTextChanged(m_streamingText);
            Q_EMIT chunkReceived(chunk);
        }
    }
}

void OllamaClient::onReplyFinished()
{
    if (!m_currentReply) return;

    const bool error = (m_currentReply->error() != QNetworkReply::NoError &&
                        m_currentReply->error() != QNetworkReply::OperationCanceledError);

    const QNetworkReply::NetworkError errCode = m_currentReply->error();
    const QString httpError = m_currentReply->errorString();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;

    if (error) {
        const bool fallbackEligible = m_streamingText.isEmpty();
        QString friendlyErr;
        if (errCode == QNetworkReply::ConnectionRefusedError || errCode == QNetworkReply::HostNotFoundError) {
            friendlyErr = QStringLiteral("Ollama no está en ejecución. Inícialo con 'ollama serve' o revisa la URL.");
        } else if (errCode == QNetworkReply::TimeoutError) {
            friendlyErr = QStringLiteral("Tiempo de espera agotado al consultar a Ollama.");
        } else {
            friendlyErr = QStringLiteral("Error al comunicarse con Ollama: %1").arg(httpError);
        }
        Q_EMIT errorOccurred(friendlyErr, fallbackEligible);
        return;
    }

    if (!m_pendingToolCalls.isEmpty()) {
        QJsonArray calls = m_pendingToolCalls;
        m_pendingToolCalls = QJsonArray();
        Q_EMIT toolCallsDetected(calls);
        return;
    }

    const QString finalResponse = m_streamingText;
    m_streamingText.clear();
    Q_EMIT finished(finalResponse);
}

} // namespace Kity
