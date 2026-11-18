/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "geministtclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "../pipewirerecorder.h"
#include "voiceutils.h"

namespace Kity {

GeminiSttClient::GeminiSttClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void GeminiSttClient::transcribe(const QByteArray &pcmData, const QString &apiKey, const QString &modelName)
{
    if (pcmData.size() < 1024) {
        Q_EMIT transcriptionFailed(QStringLiteral("No se detectó audio de voz."));
        return;
    }

    if (apiKey.isEmpty()) {
        Q_EMIT transcriptionFailed(QStringLiteral("API key de Gemini no configurada."));
        return;
    }

    const QByteArray wavData = PipeWireRecorder::pcmToWav(pcmData, 16000, 1);
    const QString base64Audio = QString::fromLatin1(wavData.toBase64());
    const QString modelToUse = (modelName.isEmpty() ? QStringLiteral("gemini-2.0-flash") : modelName);

    const QUrl geminiUrl(QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent?key=%2")
        .arg(modelToUse, apiKey));

    QJsonObject audioPart;
    QJsonObject inlineData;
    inlineData[QStringLiteral("mime_type")] = QStringLiteral("audio/wav");
    inlineData[QStringLiteral("data")] = base64Audio;
    audioPart[QStringLiteral("inline_data")] = inlineData;

    QJsonObject promptPart;
    promptPart[QStringLiteral("text")] = QStringLiteral("Transcribe exactamente este audio de voz a texto en español. Responde exclusivamente con las palabras transcritas, sin explicaciones, sin preámbulos y sin comillas.");

    QJsonArray parts;
    parts.append(audioPart);
    parts.append(promptPart);

    QJsonObject contentObj;
    contentObj[QStringLiteral("parts")] = parts;

    QJsonArray contentsArr;
    contentsArr.append(contentObj);

    QJsonObject genConfig;
    genConfig[QStringLiteral("temperature")] = 0.0;

    QJsonObject payload;
    payload[QStringLiteral("contents")] = contentsArr;
    payload[QStringLiteral("generationConfig")] = genConfig;

    QNetworkRequest req(geminiUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(8000);

    QNetworkReply *reply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT transcriptionFailed(reply->errorString());
            return;
        }

        const QByteArray responseData = reply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(responseData);
        const QJsonObject root = doc.object();
        const QJsonArray candidates = root.value(QStringLiteral("candidates")).toArray();
        if (candidates.isEmpty()) {
            Q_EMIT transcriptionFailed(QStringLiteral("Gemini no devolvió candidatos válidos."));
            return;
        }

        const QJsonObject firstCand = candidates.first().toObject();
        const QJsonObject content = firstCand.value(QStringLiteral("content")).toObject();
        const QJsonArray candParts = content.value(QStringLiteral("parts")).toArray();
        QString text;
        for (const QJsonValue &pv : candParts) {
            text += pv.toObject().value(QStringLiteral("text")).toString();
        }
        QString cleaned = VoiceUtils::cleanWhisperOutput(text);
        cleaned = VoiceUtils::removeFillerWords(cleaned, QStringLiteral("es"));

        if (cleaned.isEmpty()) {
            Q_EMIT transcriptionFailed(QStringLiteral("Transcripción vacía."));
            return;
        }

        Q_EMIT transcriptionReady(cleaned);
    });
}

} // namespace Kity
