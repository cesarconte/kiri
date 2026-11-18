/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace Kity {

class OllamaClient : public QObject
{
    Q_OBJECT

public:
    explicit OllamaClient(QNetworkAccessManager *nam, QObject *parent = nullptr);
    ~OllamaClient() override;

    bool isBusy() const;
    void abort();
    void sendChat(const QUrl &chatUrl, const QJsonObject &reqPayload, int timeoutMs);
    void refreshModels(const QUrl &tagsUrl);

    QString streamingText() const { return m_streamingText; }

Q_SIGNALS:
    void chunkReceived(const QString &chunk);
    void streamingTextChanged(const QString &fullText);
    void toolCallsDetected(const QJsonArray &toolCalls);
    void finished(const QString &finalResponse);
    void errorOccurred(const QString &errorMessage, bool fallbackEligible);
    void modelsRefreshed(const QStringList &models, bool reachable);

private Q_SLOTS:
    void onStreamChunk();
    void onReplyFinished();

private:
    QNetworkAccessManager *m_nam = nullptr;
    QPointer<QNetworkReply> m_currentReply;
    QString m_streamingText;
    QJsonArray m_pendingToolCalls;
};

} // namespace Kity
