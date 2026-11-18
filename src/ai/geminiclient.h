/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QUrl>

namespace Kity {

class GeminiClient : public QObject
{
    Q_OBJECT

public:
    explicit GeminiClient(QNetworkAccessManager *nam, QObject *parent = nullptr);
    ~GeminiClient() override;

    bool isBusy() const;
    void abort();
    void sendStream(const QUrl &url, const QJsonObject &payload);

    QString streamingText() const { return m_streamingText; }

Q_SIGNALS:
    void chunkReceived(const QString &chunk);
    void streamingTextChanged(const QString &fullText);
    void finished(const QString &finalResponse, const QList<QPair<QString, QString>> &sources);
    void errorOccurred(const QString &errorMessage, bool fallbackEligible);

private Q_SLOTS:
    void onStreamChunk();
    void onReplyFinished();

private:
    QNetworkAccessManager *m_nam = nullptr;
    QPointer<QNetworkReply> m_currentReply;
    QString m_streamingText;
    QList<QPair<QString, QString>> m_sources;
};

} // namespace Kity
