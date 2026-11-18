/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

namespace Kity {

class GeminiSttClient : public QObject
{
    Q_OBJECT

public:
    explicit GeminiSttClient(QObject *parent = nullptr);
    ~GeminiSttClient() override = default;

    void transcribe(const QByteArray &pcmData, const QString &apiKey, const QString &modelName);

Q_SIGNALS:
    void transcriptionReady(const QString &text);
    void transcriptionFailed(const QString &errorMessage);

private:
    QNetworkAccessManager *m_nam = nullptr;
};

} // namespace Kity
