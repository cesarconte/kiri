/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QQueue>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace Kity {

/**
 * @brief TtsEngine manages text-to-speech synthesis pipelines (Google Cloud TTS,
 * Microsoft Edge Neural TTS, Piper, Kokoro), sentence streaming buffers,
 * audio playback processes (pw-play), and earcon sounds.
 */
class TtsEngine : public QObject
{
    Q_OBJECT

public:
    explicit TtsEngine(QObject *parent = nullptr);
    ~TtsEngine() override;

    bool isSpeaking() const;
    void speakText(const QString &text);
    void stopSpeaking();
    void processStreamingChunk(const QString &newChunk, const QString &speechMode, bool lastQueryWasVoice);
    void flushStreamingBuffer();
    void enqueueSentence(const QString &sentence);
    void playSiriEarcon(bool activating, bool earconsEnabled);

Q_SIGNALS:
    void speakingStateChanged(bool speaking);
    void queueFinished();

private:
    void playNextItem();
    void speakSentence(const QString &sentence);
    void terminateProcesses();
    void startPipedSpeech(const QString &ttsProg, const QStringList &ttsArgs, const QString &stdinText = QString());
    void playAudioBuffer(const QByteArray &audioBytes);

    QString m_speechBuffer;
    QQueue<QString> m_queue;
    bool m_isSpeakingSentence = false;
    bool m_insideThinkingBlock = false;

    QNetworkAccessManager *m_nam = nullptr;
    QPointer<QNetworkReply> m_currentReply;
    QProcess *m_ttsProcess = nullptr;
    QPointer<QProcess> m_pwPlayProcess;
};

} // namespace Kity
