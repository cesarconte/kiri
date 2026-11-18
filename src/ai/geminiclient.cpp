/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "geminiclient.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>

namespace Kity {

GeminiClient::GeminiClient(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_nam(nam)
{
}

GeminiClient::~GeminiClient()
{
    abort();
}

bool GeminiClient::isBusy() const
{
    return m_currentReply && m_currentReply->isRunning();
}

void GeminiClient::abort()
{
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    m_streamingText.clear();
    m_sources.clear();
}

void GeminiClient::sendStream(const QUrl &url, const QJsonObject &payload)
{
    abort();

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    m_currentReply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(m_currentReply, &QNetworkReply::readyRead, this, &GeminiClient::onStreamChunk);
    connect(m_currentReply, &QNetworkReply::finished, this, &GeminiClient::onReplyFinished);
}

void GeminiClient::onStreamChunk()
{
    if (!m_currentReply) return;

    while (m_currentReply->canReadLine()) {
        QByteArray line = m_currentReply->readLine().trimmed();
        if (line.isEmpty()) continue;

        if (line.startsWith("data: ")) {
            line = line.mid(6).trimmed();
        }

        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) continue;

        const QJsonObject obj = doc.object();
        if (obj.contains(QStringLiteral("error"))) {
            const QJsonObject errObj = obj.value(QStringLiteral("error")).toObject();
            const QString errMsg = errObj.value(QStringLiteral("message")).toString();
            qWarning() << "Gemini stream error:" << errMsg;
            Q_EMIT errorOccurred(QStringLiteral("Error de Google Gemini: %1").arg(errMsg), true);
            return;
        }

        const QJsonArray candidates = obj.value(QStringLiteral("candidates")).toArray();
        if (candidates.isEmpty()) continue;

        const QJsonObject candidate = candidates.first().toObject();

        if (candidate.contains(QStringLiteral("groundingMetadata"))) {
            const QJsonObject gMeta = candidate.value(QStringLiteral("groundingMetadata")).toObject();
            if (gMeta.contains(QStringLiteral("groundingChunks"))) {
                const QJsonArray chunks = gMeta.value(QStringLiteral("groundingChunks")).toArray();
                for (const QJsonValue &chunkVal : chunks) {
                    const QJsonObject webObj = chunkVal.toObject().value(QStringLiteral("web")).toObject();
                    const QString uri = webObj.value(QStringLiteral("uri")).toString();
                    QString title = webObj.value(QStringLiteral("title")).toString();
                    if (title.isEmpty()) {
                        title = QUrl(uri).host();
                    }
                    if (!uri.isEmpty()) {
                        bool exists = false;
                        for (const auto &pair : m_sources) {
                            if (pair.second == uri) {
                                exists = true;
                                break;
                            }
                        }
                        if (!exists) {
                            m_sources.append(qMakePair(title, uri));
                        }
                    }
                }
            }
        }

        const QJsonObject content = candidate.value(QStringLiteral("content")).toObject();
        const QJsonArray parts = content.value(QStringLiteral("parts")).toArray();
        for (const QJsonValue &partVal : parts) {
            const QJsonObject partObj = partVal.toObject();
            const QString textChunk = partObj.value(QStringLiteral("text")).toString();
            if (!textChunk.isEmpty()) {
                m_streamingText.append(textChunk);
                Q_EMIT streamingTextChanged(m_streamingText);
                Q_EMIT chunkReceived(textChunk);
            }
        }
    }
}

void GeminiClient::onReplyFinished()
{
    if (!m_currentReply) return;

    const int statusCode = m_currentReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool error = (statusCode >= 400 || (m_currentReply->error() != QNetworkReply::NoError &&
                        m_currentReply->error() != QNetworkReply::OperationCanceledError));

    const QString httpError = m_currentReply->errorString();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;

    if (error || m_streamingText.trimmed().isEmpty()) {
        qWarning() << "Kity: Gemini failed or empty (" << httpError << ")";
        Q_EMIT errorOccurred(QStringLiteral("Error al comunicarse con Google Gemini: %1").arg(httpError), true);
        return;
    }

    const QString finalResponse = m_streamingText;
    const QList<QPair<QString, QString>> sources = m_sources;
    m_streamingText.clear();
    m_sources.clear();
    Q_EMIT finished(finalResponse, sources);
}

} // namespace Kity
