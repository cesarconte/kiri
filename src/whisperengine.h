/*
    SPDX-FileCopyrightText: 2026 Cesar
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QThreadPool>

#include <atomic>
#include <vector>

struct whisper_context;

class WhisperEngine : public QObject
{
    Q_OBJECT

public:
    explicit WhisperEngine(QObject *parent = nullptr);
    ~WhisperEngine() override;

    bool loadModel(const QString &modelPath);
    void loadModelAsync(const QString &modelPath);
    bool isModelLoaded() const;
    QString currentModelPath() const;

    void transcribeAsync(const QByteArray &pcmS16Data,
                         const QString &language = QStringLiteral("es"),
                         const QString &initialPrompt = QString());
    QString transcribeSync(const QByteArray &pcmS16Data,
                          const QString &language = QStringLiteral("es"),
                          const QString &initialPrompt = QString());

    static QString findDefaultModel();
    static QStringList discoverModels();
    static std::vector<float> pcmS16ToFloat(const QByteArray &pcmS16Data);

Q_SIGNALS:
    void transcriptionReady(const QString &rawTranscript);
    void transcriptionFailed(const QString &errorMessage);
    void modelLoaded(const QString &modelPath);
    void modelLoadFailed(const QString &errorMessage);

private:
    QString runInference(const std::vector<float> &pcmf32,
                         const QString &language,
                         const QString &initialPrompt = QString());

    whisper_context *m_ctx = nullptr;
    mutable QMutex m_mutex;
    QThreadPool m_threadPool;
    QString m_currentModelPath;
    std::atomic<bool> m_modelLoaded{false};
    std::atomic<bool> m_transcribing{false};
};
