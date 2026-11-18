/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <memory>

#include "../pipewirerecorder.h"
#include "../whisperengine.h"
#include "../kdictationclient.h"
#include "geministtclient.h"
#include "ttsengine.h"
#include "voiceutils.h"

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
 * Whisper STT, centralized STT fallback, Wake-Word detection, and delegates
 * TTS synthesis to TtsEngine and remote STT to GeminiSttClient.
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

    // Text to Speech (delegates to TtsEngine)
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

    static QString cleanWhisperOutput(const QString &raw) { return VoiceUtils::cleanWhisperOutput(raw); }
    static QString removeFillerWords(const QString &text, const QString &lang = QStringLiteral("es")) { return VoiceUtils::removeFillerWords(text, lang); }
    static QString stripMarkdownForSpeech(const QString &markdown) { return VoiceUtils::stripMarkdownForSpeech(markdown); }
    static QByteArray createWavHeader(int dataSize, int sampleRate, int channels, int bitsPerSample) { return VoiceUtils::createWavHeader(dataSize, sampleRate, channels, bitsPerSample); }

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
    void checkWakeWordTranscription(const QByteArray &pcmData);

    VoiceConfig m_config;

    std::unique_ptr<PipeWireRecorder> m_voiceRecorder;
    std::unique_ptr<PipeWireRecorder> m_wakeWordRecorder;
    std::unique_ptr<WhisperEngine> m_whisperEngine;
    std::unique_ptr<KDictationClient> m_kdictationClient;
    std::unique_ptr<GeminiSttClient> m_geminiSttClient;
    std::unique_ptr<TtsEngine> m_ttsEngine;

    QTimer *m_centralizedFallbackTimer = nullptr;
    bool m_waitingForCentralizedSTT = false;
    quint64 m_sttSessionId = 0;

    QByteArray m_recordedPcmBuffer;
    QElapsedTimer m_recordingDurationTimer;
    qreal m_audioLevel = 0.0;
    bool m_isRecording = false;
    int m_bargeInVoiceFrames = 0;

    QByteArray m_wakeWordPcmBuffer;
    bool m_wakeWordEvaluating = false;
    QElapsedTimer m_wakeWordCooldown;
    bool m_wakeWordListening = false;

    QTimer *m_vadTimer = nullptr;
    QElapsedTimer m_speechSilenceTimer;
    bool m_speechDetected = false;
    bool m_lastQueryWasVoice = false;
};

} // namespace Kity
