/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QQueue>
#include <QString>
#include <QTimer>
#include <memory>

#include "../pipewirerecorder.h"
#include "../whisperengine.h"
#include "../kdictationclient.h"

namespace Kity {

struct VoiceConfig {
    QString sttEngine = QStringLiteral("whisper"); // "whisper", "gemini", "centralized"
    QString whisperModelPath;
    QString whisperLanguage = QStringLiteral("es");
    QString piperVoicePath;
    QString speechOutputMode = QStringLiteral("always"); // "always", "voice_only", "manual"
    bool vadEnabled = true;
    bool earconsEnabled = true;
    bool useCentralizedVoice = false;
    QString geminiApiKey;
};

/**
 * @brief VoiceEngine manages audio capture, Voice Activity Detection (VAD),
 * Whisper STT, centralized STT fallback, Wake-Word detection, and Piper/Edge TTS.
 */
class VoiceEngine : public QObject
{
    Q_OBJECT

public:
    static VoiceEngine &instance();

    void setConfig(const VoiceConfig &config);
    VoiceConfig config() const;

    // Recording & STT
    bool isRecording() const;
    qreal audioLevel() const;
    void startRecording();
    void stopRecording();
    void toggleRecording();

    // Wake-word
    bool isWakeWordListening() const;
    void startWakeWordListener();
    void stopWakeWordListener();

    // Text to Speech
    bool isSpeaking() const;
    void speakText(const QString &text);
    void stopSpeaking();
    void processStreamingTtsChunk(const QString &chunk);
    void enqueueSentenceForSpeech(const QString &sentence);

    // Dictation & Audio File Handling
    void transcribePcm(const QByteArray &pcmData);
    void transcribeWavFile(const QString &wavPath);
    void injectDictationPcm(const QByteArray &pcmData);
    void injectDictationText(const QString &text);

    // Subsystem status & model loading
    bool isWhisperModelLoaded() const;
    bool isKDictationAvailable() const;
    void checkKDictationAvailability();
    void preloadWhisperModel(const QString &modelPath);

    // Audio cues & utilities
    bool isPipeWireEchoCancelAvailable() const;
    QString resolvePreferredCaptureSource(bool *isBluetoothOut = nullptr) const;
    void startVad();
    void stopVad();
    void playSiriEarcon(bool activating);
    static QString cleanWhisperOutput(const QString &raw);
    static QString removeFillerWords(const QString &text, const QString &lang = QStringLiteral("es"));
    static QString stripMarkdownForSpeech(const QString &markdown);
    static QByteArray createWavHeader(int dataSize, int sampleRate, int channels, int bitsPerSample);

Q_SIGNALS:
    void transcriptionReady(const QString &text, bool fromVoice);
    void transcriptionFailed(const QString &errorMessage);
    void audioLevelChanged(qreal level);
    void recordingStateChanged(bool recording);
    void speakingStateChanged(bool speaking);
    void wakeWordTriggered();
    void dictationInjected(const QString &text);
    void kdictationAvailabilityChanged(bool available);
    void whisperModelStatusChanged(bool loaded);

private Q_SLOTS:
    void onVoiceAudioChunkReady(const QByteArray &chunk, double level);
    void onWakeWordAudioChunk(const QByteArray &chunk, double level);
    void onWhisperTranscriptionReady(const QString &rawTranscript);
    void onWhisperTranscriptionFailed(const QString &errorMessage);
    void onCentralizedTranscriptionReady(const QString &rawTranscript);
    void onCentralizedTranscriptionFailed(const QString &errorMessage);
    void onCentralizedSTTTimeout();

private:
    explicit VoiceEngine(QObject *parent = nullptr);
    ~VoiceEngine() override;

    VoiceEngine(const VoiceEngine &) = delete;
    VoiceEngine &operator=(const VoiceEngine &) = delete;

    void runSpeechTranscription(const QByteArray &pcmData = QByteArray());
    void runWhisperTranscription(const QByteArray &pcmData);
    void runGeminiAudioTranscription(const QByteArray &pcmData);
    void checkWakeWordTranscription(const QByteArray &pcmData);

    void playNextTtsQueueItem();
    void speakSentence(const QString &sentence);

    VoiceConfig m_config;

    std::unique_ptr<PipeWireRecorder> m_voiceRecorder;
    std::unique_ptr<PipeWireRecorder> m_wakeWordRecorder;
    std::unique_ptr<WhisperEngine> m_whisperEngine;
    std::unique_ptr<KDictationClient> m_kdictationClient;

    QTimer *m_centralizedFallbackTimer = nullptr;
    bool m_waitingForCentralizedSTT = false;
    quint64 m_sttSessionId = 0;

    QByteArray m_recordedPcmBuffer;
    QElapsedTimer m_recordingDurationTimer;
    qreal m_audioLevel = 0.0;
    bool m_isRecording = false;

    QProcess *m_ttsProcess = nullptr;
    QPointer<QProcess> m_pwPlayProcess;
    int m_bargeInVoiceFrames = 0;

    QByteArray m_wakeWordPcmBuffer;
    bool m_wakeWordEvaluating = false;
    QElapsedTimer m_wakeWordCooldown;
    bool m_wakeWordListening = false;

    QTimer *m_vadTimer = nullptr;
    QElapsedTimer m_speechSilenceTimer;
    bool m_speechDetected = false;

    QString m_ttsSpeechBuffer;
    QQueue<QString> m_ttsQueue;
    bool m_isSpeakingSentence = false;
    bool m_insideThinkingBlock = false;
    bool m_lastQueryWasVoice = false;

    QNetworkAccessManager *m_nam = nullptr;
    QPointer<QNetworkReply> m_currentTtsReply;
};

} // namespace Kity
