/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ttsengine.h"
#include "voiceutils.h"

#include <cmath>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#include <KConfigGroup>
#include <KSharedConfig>

namespace Kity {

namespace {

QString readConfigString(const QString &key, const QString &def = QString())
{
    const KSharedConfig::Ptr kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    const KConfigGroup cfg = kityrc->group(QStringLiteral("General"));
    return cfg.readEntry(key, def);
}

int readConfigInt(const QString &key, int def = 0)
{
    const KSharedConfig::Ptr kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    const KConfigGroup cfg = kityrc->group(QStringLiteral("General"));
    return cfg.readEntry(key, def);
}

double readConfigDouble(const QString &key, double def = 0.0)
{
    const KSharedConfig::Ptr kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    const KConfigGroup cfg = kityrc->group(QStringLiteral("General"));
    return cfg.readEntry(key, def);
}



QString resolvePiperVoicePath()
{
    QString path = readConfigString(QStringLiteral("piperVoicePath"));
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        const QStringList candidates = {
            QDir::homePath() + QStringLiteral("/.local/share/kity/models/es_ES-davefx-medium.onnx"),
            QDir::homePath() + QStringLiteral("/.local/share/kity/models/es_ES-sharvard-medium.onnx")
        };
        for (const QString &cand : candidates) {
            if (QFileInfo::exists(cand)) {
                return cand;
            }
        }
    }
    return path;
}

} // anonymous namespace

TtsEngine::TtsEngine(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

TtsEngine::~TtsEngine()
{
    stopSpeaking();
}

bool TtsEngine::isSpeaking() const
{
    return m_isSpeakingSentence || (m_pwPlayProcess && m_pwPlayProcess->state() == QProcess::Running)
        || (m_ttsProcess && m_ttsProcess->state() == QProcess::Running);
}

void TtsEngine::speakText(const QString &text)
{
    stopSpeaking();
    const QString clean = VoiceUtils::stripMarkdownForSpeech(text);
    if (!clean.isEmpty()) {
        enqueueSentence(clean);
    }
}

void TtsEngine::stopSpeaking()
{
    m_queue.clear();
    m_speechBuffer.clear();
    m_insideThinkingBlock = false;

    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

    if (m_pwPlayProcess) {
        if (m_pwPlayProcess->state() == QProcess::Running) {
            m_pwPlayProcess->kill();
        }
        m_pwPlayProcess->deleteLater();
        m_pwPlayProcess = nullptr;
    }

    if (m_ttsProcess) {
        if (m_ttsProcess->state() == QProcess::Running) {
            m_ttsProcess->kill();
        }
        m_ttsProcess->deleteLater();
        m_ttsProcess = nullptr;
    }

    m_isSpeakingSentence = false;
    Q_EMIT speakingStateChanged(false);
}

void TtsEngine::processStreamingChunk(const QString &newChunk, const QString &speechMode, bool lastQueryWasVoice)
{
    if (speechMode == QLatin1String("manual")) {
        return;
    }
    if (speechMode == QLatin1String("voice_only") && !lastQueryWasVoice) {
        return;
    }

    // Filter <think> blocks in reasoning models
    if (newChunk.contains(QLatin1String("<think>"))) {
        m_insideThinkingBlock = true;
    }
    m_speechBuffer.append(newChunk);
    if (m_insideThinkingBlock) {
        const int closeIdx = m_speechBuffer.indexOf(QLatin1String("</think>"));
        if (closeIdx != -1) {
            m_speechBuffer.remove(0, closeIdx + 8);
            m_insideThinkingBlock = false;
        } else {
            return;
        }
    }

    // Regex 1: Full sentence terminators (. ? ! or newline)
    static const QRegularExpression sentenceTerminator(
        QStringLiteral("(?:[?!\\n]+|(?<!\\b(?:ej|etc|p\\.ej|sr|sra|dr|dra|vs))(?<!\\d)[.](?!\\d)(?:\\s+|$))"),
        QRegularExpression::CaseInsensitiveOption
    );

    // Regex 2: Clause pause boundaries (, ; : —) for sub-sentence low latency streaming
    static const QRegularExpression clauseTerminator(
        QStringLiteral("(?<!\\d)[,;:—]+(?!\\d)(?:\\s+|$)")
    );

    bool extracted = true;
    while (extracted) {
        extracted = false;

        QRegularExpressionMatch sentMatch = sentenceTerminator.match(m_speechBuffer);
        if (sentMatch.hasMatch()) {
            const int endPos = sentMatch.capturedEnd();
            const QString sentence = m_speechBuffer.left(endPos).trimmed();
            m_speechBuffer = m_speechBuffer.mid(endPos);

            const QString clean = VoiceUtils::stripMarkdownForSpeech(sentence);
            if (!clean.isEmpty()) {
                enqueueSentence(clean);
            }
            extracted = true;
            continue;
        }

        if (m_speechBuffer.length() >= 35) {
            QRegularExpressionMatch clauseMatch = clauseTerminator.match(m_speechBuffer);
            if (clauseMatch.hasMatch()) {
                const int endPos = clauseMatch.capturedEnd();
                if (endPos >= 25) {
                    const QString clause = m_speechBuffer.left(endPos).trimmed();
                    m_speechBuffer = m_speechBuffer.mid(endPos);

                    const QString clean = VoiceUtils::stripMarkdownForSpeech(clause);
                    if (!clean.isEmpty()) {
                        enqueueSentence(clean);
                    }
                    extracted = true;
                }
            }
        }
    }
}

void TtsEngine::enqueueSentence(const QString &sentence)
{
    m_queue.enqueue(sentence);
    if (!m_isSpeakingSentence) {
        playNextItem();
    }
}

void TtsEngine::playNextItem()
{
    if (m_queue.isEmpty()) {
        m_isSpeakingSentence = false;
        Q_EMIT speakingStateChanged(false);
        Q_EMIT queueFinished();
        return;
    }

    const QString sentence = m_queue.dequeue();
    speakSentence(sentence);
}

void TtsEngine::speakSentence(const QString &sentence)
{
    const QString pwPlay = QStandardPaths::findExecutable(QStringLiteral("pw-play"));
    if (pwPlay.isEmpty() || sentence.trimmed().isEmpty()) {
        m_isSpeakingSentence = false;
        playNextItem();
        return;
    }

    const QString ttsEngine = readConfigString(QStringLiteral("ttsEngine"), QStringLiteral("google")).toLower();
    const QString googleApiKey = readConfigString(QStringLiteral("googleApiKey")).trimmed();
    const QString edgeCli = QStandardPaths::findExecutable(QStringLiteral("kity-edge-tts"));
    const QString kokoroCli = QStandardPaths::findExecutable(QStringLiteral("kity-kokoro-tts"));
    const QString piperCli = QStandardPaths::findExecutable(QStringLiteral("piper"));
    const QString piperVoicePath = resolvePiperVoicePath();

    const QString voiceAccent = readConfigString(QStringLiteral("voiceAccent"), QStringLiteral("Spanish (Spain)"));
    const int voiceIndex = qBound(0, readConfigInt(QStringLiteral("voiceIndex"), 3), 6);
    const double voicePace = qBound(0.0, readConfigDouble(QStringLiteral("voicePace"), 0.55), 1.0);
    const double voiceExpressivity = qBound(0.0, readConfigDouble(QStringLiteral("voiceExpressivity"), 0.65), 1.0);

    auto resolveEdgeVoiceForOrb = [](const QString &accent, int idx, int &orbPitchShift) -> QString {
        orbPitchShift = 0;
        if (accent.contains(QLatin1String("Latin"), Qt::CaseInsensitive)) {
            static const QStringList voices = {
                QStringLiteral("es-MX-DaliaNeural"), QStringLiteral("es-MX-JorgeNeural"),
                QStringLiteral("es-CO-SalomeNeural"), QStringLiteral("es-AR-ElenaNeural"),
                QStringLiteral("es-CL-CatalinaNeural"), QStringLiteral("es-US-PalomaNeural"),
                QStringLiteral("es-PE-CamilaNeural")
            };
            return voices.value(idx, voices.at(0));
        }
        if (accent.contains(QLatin1String("American"), Qt::CaseInsensitive)) {
            static const QStringList voices = {
                QStringLiteral("en-US-AndrewNeural"), QStringLiteral("en-US-BrianNeural"),
                QStringLiteral("en-US-GuyNeural"), QStringLiteral("en-US-AvaNeural"),
                QStringLiteral("en-US-EmmaNeural"), QStringLiteral("en-US-JennyNeural"),
                QStringLiteral("en-US-AriaNeural")
            };
            return voices.value(idx, voices.at(3));
        }
        if (accent.contains(QLatin1String("British"), Qt::CaseInsensitive)) {
            static const QStringList voices = {
                QStringLiteral("en-GB-RyanNeural"), QStringLiteral("en-GB-ThomasNeural"),
                QStringLiteral("en-GB-RyanNeural"), QStringLiteral("en-GB-SoniaNeural"),
                QStringLiteral("en-GB-MaisieNeural"), QStringLiteral("en-GB-LibbyNeural"),
                QStringLiteral("en-GB-LibbyNeural")
            };
            static const int shifts[] = { -3, 0, +4, 0, 0, 0, +4 };
            orbPitchShift = shifts[qBound(0, idx, 6)];
            return voices.value(idx, voices.at(3));
        }
        static const QStringList esVoices = {
            QStringLiteral("es-ES-AlvaroNeural"), QStringLiteral("es-ES-AlvaroNeural"),
            QStringLiteral("es-ES-AlvaroNeural"), QStringLiteral("es-ES-ElviraNeural"),
            QStringLiteral("es-ES-ElviraNeural"), QStringLiteral("es-ES-XimenaNeural"),
            QStringLiteral("es-ES-XimenaNeural")
        };
        static const int esShifts[] = { -6, 0, +5, 0, +4, -2, +4 };
        orbPitchShift = esShifts[qBound(0, idx, 6)];
        return esVoices.value(idx, esVoices.at(3));
    };

    auto resolveGoogleVoiceForOrb = [](const QString &accent, int idx, QString &langCodeOut) -> QString {
        if (accent.contains(QLatin1String("Latin"), Qt::CaseInsensitive)) {
            langCodeOut = QStringLiteral("es-US");
            static const QStringList voices = {
                QStringLiteral("es-US-Neural2-A"), QStringLiteral("es-US-Neural2-B"),
                QStringLiteral("es-US-Neural2-C"), QStringLiteral("es-US-Neural2-A"),
                QStringLiteral("es-US-Studio-B"), QStringLiteral("es-US-Wavenet-A"),
                QStringLiteral("es-US-Wavenet-B")
            };
            return voices.value(idx, voices.at(3));
        }
        if (accent.contains(QLatin1String("American"), Qt::CaseInsensitive)) {
            langCodeOut = QStringLiteral("en-US");
            static const QStringList voices = {
                QStringLiteral("en-US-Neural2-C"), QStringLiteral("en-US-Neural2-D"),
                QStringLiteral("en-US-Neural2-E"), QStringLiteral("en-US-Neural2-F"),
                QStringLiteral("en-US-Neural2-G"), QStringLiteral("en-US-Neural2-H"),
                QStringLiteral("en-US-Neural2-I")
            };
            return voices.value(idx, voices.at(3));
        }
        if (accent.contains(QLatin1String("British"), Qt::CaseInsensitive)) {
            langCodeOut = QStringLiteral("en-GB");
            static const QStringList voices = {
                QStringLiteral("en-GB-Neural2-A"), QStringLiteral("en-GB-Neural2-B"),
                QStringLiteral("en-GB-Neural2-C"), QStringLiteral("en-GB-Neural2-D"),
                QStringLiteral("en-GB-Neural2-F"), QStringLiteral("en-GB-Wavenet-A"),
                QStringLiteral("en-GB-Wavenet-B")
            };
            return voices.value(idx, voices.at(3));
        }
        langCodeOut = QStringLiteral("es-ES");
        static const QStringList esVoices = {
            QStringLiteral("es-ES-Neural2-B"), QStringLiteral("es-ES-Neural2-A"),
            QStringLiteral("es-ES-Neural2-C"), QStringLiteral("es-ES-Neural2-F"),
            QStringLiteral("es-ES-Neural2-D"), QStringLiteral("es-ES-Neural2-E"),
            QStringLiteral("es-ES-Neural2-F")
        };
        return esVoices.value(idx, esVoices.at(3));
    };

    // 1. Google Cloud Text-to-Speech API
    if (ttsEngine == QLatin1String("google") && !googleApiKey.isEmpty()) {
        m_isSpeakingSentence = true;
        Q_EMIT speakingStateChanged(true);

        QString langCode = QStringLiteral("es-ES");
        const QString googleVoice = resolveGoogleVoiceForOrb(voiceAccent, voiceIndex, langCode);
        QJsonObject inputObj;
        inputObj[QStringLiteral("text")] = sentence;

        QJsonObject voiceObj;
        voiceObj[QStringLiteral("languageCode")] = langCode;
        voiceObj[QStringLiteral("name")] = googleVoice;

        QJsonObject audioConfig;
        audioConfig[QStringLiteral("audioEncoding")] = QStringLiteral("MP3");
        audioConfig[QStringLiteral("speakingRate")] = 0.65 + (voicePace * 0.75);
        audioConfig[QStringLiteral("pitch")] = (voiceExpressivity - 0.5) * 10.0;

        QJsonObject payload;
        payload[QStringLiteral("input")] = inputObj;
        payload[QStringLiteral("voice")] = voiceObj;
        payload[QStringLiteral("audioConfig")] = audioConfig;

        const QUrl gUrl(QStringLiteral("https://texttospeech.googleapis.com/v1/text:synthesize?key=%1").arg(googleApiKey));
        QNetworkRequest req(gUrl);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

        m_currentReply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        connect(m_currentReply.data(), &QNetworkReply::finished, this, [this, pwPlay, edgeCli, resolveEdgeVoiceForOrb, voiceAccent, voiceIndex, voicePace, voiceExpressivity, sentence]() {
            if (!m_currentReply) {
                return;
            }
            QNetworkReply *gReply = m_currentReply;
            m_currentReply = nullptr;
            gReply->deleteLater();

            if (!m_isSpeakingSentence) {
                return;
            }

            if (gReply->error() != QNetworkReply::NoError) {
                qWarning() << "Kity: Google Cloud TTS error:" << gReply->errorString() << "- Falling back to Edge-TTS";
                if (!edgeCli.isEmpty()) {
                    int orbPitchShift = 0;
                    const QString edgeVoice = resolveEdgeVoiceForOrb(voiceAccent, voiceIndex, orbPitchShift);
                    const int ratePct = qRound((voicePace - 0.5) * 80.0);
                    const int pitchHz = qRound((voiceExpressivity - 0.5) * 28.0);
                    const int totalPitchHz = pitchHz + orbPitchShift;
                    const QString rateArg = QStringLiteral("--rate=%1%2%").arg(ratePct >= 0 ? QStringLiteral("+") : QString()).arg(ratePct);
                    const QString pitchArg = QStringLiteral("--pitch=%1%2Hz").arg(totalPitchHz >= 0 ? QStringLiteral("+") : QString()).arg(totalPitchHz);

                    m_ttsProcess = new QProcess(this);
                    m_pwPlayProcess = new QProcess(this);
                    m_ttsProcess->setStandardOutputProcess(m_pwPlayProcess.data());

                    connect(m_pwPlayProcess.data(), &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
                        if (m_pwPlayProcess) {
                            m_pwPlayProcess->deleteLater();
                            m_pwPlayProcess = nullptr;
                        }
                        if (m_ttsProcess) {
                            m_ttsProcess->deleteLater();
                            m_ttsProcess = nullptr;
                        }
                        playNextItem();
                    });

                    m_ttsProcess->start(edgeCli, {QStringLiteral("--voice"), edgeVoice, rateArg, pitchArg, QStringLiteral("--text"), sentence, QStringLiteral("--write-media"), QStringLiteral("-")});
                    m_pwPlayProcess->start(pwPlay, {QStringLiteral("-")});
                    return;
                }
                playNextItem();
                return;
            }

            const QJsonDocument doc = QJsonDocument::fromJson(gReply->readAll());
            const QString b64 = doc.object().value(QStringLiteral("audioContent")).toString();
            if (b64.isEmpty()) {
                playNextItem();
                return;
            }

            const QByteArray audioBytes = QByteArray::fromBase64(b64.toUtf8());
            m_pwPlayProcess = new QProcess(this);
            connect(m_pwPlayProcess.data(), &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
                if (m_pwPlayProcess) {
                    m_pwPlayProcess->deleteLater();
                    m_pwPlayProcess = nullptr;
                }
                playNextItem();
            });

            m_pwPlayProcess->start(pwPlay, QStringList() << QStringLiteral("-"));
            m_pwPlayProcess->write(audioBytes);
            m_pwPlayProcess->closeWriteChannel();
        });
        return;
    }

    // 2. Microsoft Edge Neural TTS
    if ((ttsEngine == QLatin1String("edge") || (ttsEngine == QLatin1String("google") && googleApiKey.isEmpty())) && !edgeCli.isEmpty()) {
        m_isSpeakingSentence = true;
        Q_EMIT speakingStateChanged(true);

        int orbPitchShift = 0;
        const QString edgeVoice = resolveEdgeVoiceForOrb(voiceAccent, voiceIndex, orbPitchShift);
        const int ratePct = qRound((voicePace - 0.5) * 80.0);
        const int pitchHz = qRound((voiceExpressivity - 0.5) * 28.0);
        const int totalPitchHz = pitchHz + orbPitchShift;
        const QString rateArg = QStringLiteral("--rate=%1%2%").arg(ratePct >= 0 ? QStringLiteral("+") : QString()).arg(ratePct);
        const QString pitchArg = QStringLiteral("--pitch=%1%2Hz").arg(totalPitchHz >= 0 ? QStringLiteral("+") : QString()).arg(totalPitchHz);

        m_ttsProcess = new QProcess(this);
        m_pwPlayProcess = new QProcess(this);
        m_ttsProcess->setStandardOutputProcess(m_pwPlayProcess.data());

        QStringList edgeArgs;
        edgeArgs << QStringLiteral("--voice") << edgeVoice
                 << rateArg
                 << pitchArg
                 << QStringLiteral("--text") << sentence
                 << QStringLiteral("--write-media") << QStringLiteral("-");

        connect(m_pwPlayProcess.data(), &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
            if (m_pwPlayProcess) {
                m_pwPlayProcess->deleteLater();
                m_pwPlayProcess = nullptr;
            }
            if (m_ttsProcess) {
                m_ttsProcess->deleteLater();
                m_ttsProcess = nullptr;
            }
            playNextItem();
        });

        m_ttsProcess->start(edgeCli, edgeArgs);
        m_pwPlayProcess->start(pwPlay, QStringList() << QStringLiteral("-"));
        return;
    }

    // 3. Local TTS engines: Kokoro-82M or Piper
    QString execProg;
    QStringList execArgs;

    if (ttsEngine == QLatin1String("kokoro") && !kokoroCli.isEmpty()) {
        execProg = kokoroCli;
        const QString kokoroVoice = readConfigString(QStringLiteral("kokoroVoice"), QStringLiteral("ef_dora"));
        execArgs << QStringLiteral("--voice") << kokoroVoice
                 << QStringLiteral("--output_file") << QStringLiteral("-");
    } else if (!piperCli.isEmpty() && !piperVoicePath.isEmpty()) {
        execProg = piperCli;
        const double lengthScale = 1.35 - (voicePace * 0.65);
        const double noiseScale = 0.35 + (voiceExpressivity * 0.50);
        execArgs << QStringLiteral("--model") << piperVoicePath
                 << QStringLiteral("--length_scale") << QString::number(lengthScale, 'f', 2)
                 << QStringLiteral("--noise_scale") << QString::number(noiseScale, 'f', 2)
                 << QStringLiteral("--output_file") << QStringLiteral("-");
    } else if (!kokoroCli.isEmpty()) {
        execProg = kokoroCli;
        execArgs << QStringLiteral("--voice") << QStringLiteral("ef_dora")
                 << QStringLiteral("--output_file") << QStringLiteral("-");
    } else {
        m_isSpeakingSentence = false;
        Q_EMIT speakingStateChanged(false);
        playNextItem();
        return;
    }

    m_isSpeakingSentence = true;
    Q_EMIT speakingStateChanged(true);

    m_ttsProcess = new QProcess(this);
    m_pwPlayProcess = new QProcess(this);
    m_ttsProcess->setStandardOutputProcess(m_pwPlayProcess.data());

    connect(m_pwPlayProcess.data(), &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        if (m_pwPlayProcess) {
            m_pwPlayProcess->deleteLater();
            m_pwPlayProcess = nullptr;
        }
        if (m_ttsProcess) {
            m_ttsProcess->deleteLater();
            m_ttsProcess = nullptr;
        }
        playNextItem();
    });

    m_ttsProcess->start(execProg, execArgs);
    m_pwPlayProcess->start(pwPlay, QStringList() << QStringLiteral("-"));

    m_ttsProcess->write(sentence.toUtf8());
    m_ttsProcess->closeWriteChannel();
}

void TtsEngine::playSiriEarcon(bool activating, bool earconsEnabled)
{
    if (!earconsEnabled) {
        return;
    }

    const QString pwPlay = QStandardPaths::findExecutable(QStringLiteral("pw-play"));
    if (pwPlay.isEmpty()) {
        return;
    }

    const QString fileName = activating ? QStringLiteral("kity_on.wav") : QStringLiteral("kity_off.wav");
    QString earconPath = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("kity/sounds/") + fileName);
    if (earconPath.isEmpty()) {
        earconPath = QDir::homePath() + QStringLiteral("/.local/share/kity/sounds/") + fileName;
    }

    if (!QFileInfo::exists(earconPath)) {
        earconPath = QDir::tempPath() + (activating ? QStringLiteral("/kity_earcon_on.wav") : QStringLiteral("/kity_earcon_off.wav"));
        if (!QFileInfo::exists(earconPath)) {
            const int sampleRate = 44100;
            const double durationSec = 0.16;
            const int totalSamples = static_cast<int>(sampleRate * durationSec);
            QByteArray pcm(totalSamples * static_cast<int>(sizeof(qint16)), 0);
            qint16 *samples = reinterpret_cast<qint16 *>(pcm.data());

            const double f1 = activating ? 987.77 : 1318.51;
            const double f2 = activating ? 1318.51 : 987.77;
            const double tSwitch = 0.055;
            const double twoPi = 6.283185307179586;

            for (int i = 0; i < totalSamples; ++i) {
                const double t = static_cast<double>(i) / sampleRate;
                const double env1 = (t < 0.09) ? std::exp(-t / 0.035) : 0.0;
                double env2 = 0.0;
                if (t >= tSwitch) {
                    const double t2 = t - tSwitch;
                    const double attack = qMin(1.0, t2 / 0.005);
                    env2 = attack * std::exp(-t2 / 0.055);
                }

                const double tone1 = std::sin(twoPi * f1 * t) + 0.25 * std::sin(4.0 * M_PI * f1 * t);
                const double t2 = qMax(0.0, t - tSwitch);
                const double tone2 = std::sin(twoPi * f2 * t2) + 0.20 * std::sin(4.0 * M_PI * f2 * t2);

                double val = (tone1 * env1 * 0.18) + (tone2 * env2 * 0.22);
                val = qBound(-1.0, val, 1.0);
                samples[i] = static_cast<qint16>(val * 32767.0);
            }

            QFile f(earconPath);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(VoiceUtils::createWavHeader(pcm.size(), sampleRate, 1, 16));
                f.write(pcm);
                f.close();
            }
        }
    }

    if (QFileInfo::exists(earconPath)) {
        QProcess::startDetached(pwPlay, {earconPath});
    }
}

} // namespace Kity
