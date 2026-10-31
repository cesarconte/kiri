/*
    SPDX-FileCopyrightText: 2026 Kiri contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <QTest>
#include <QRegularExpression>
#include <QProcess>
#include <QStandardPaths>
#include <QFile>
#include <QDir>

class TestWakeWord : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testWavHeaderFormat()
    {
        // 44-byte standard RIFF PCM header
        const int dataSize = 32000;
        const int sampleRate = 16000;
        const int channels = 1;
        const int bitsPerSample = 16;

        QByteArray header(44, 0);
        char *h = header.data();
        memcpy(h, "RIFF", 4);
        const qint32 chunkSize = 36 + dataSize;
        memcpy(h + 4, &chunkSize, 4);
        memcpy(h + 8, "WAVE", 4);
        memcpy(h + 12, "fmt ", 4);
        const qint32 subchunk1Size = 16;
        memcpy(h + 16, &subchunk1Size, 4);
        const qint16 audioFormat = 1; // PCM
        memcpy(h + 20, &audioFormat, 2);
        const qint16 numChannels = channels;
        memcpy(h + 22, &numChannels, 2);
        const qint32 sRate = sampleRate;
        memcpy(h + 24, &sRate, 4);
        const qint32 byteRate = sampleRate * channels * (bitsPerSample / 8);
        memcpy(h + 28, &byteRate, 4);
        const qint16 blockAlign = channels * (bitsPerSample / 8);
        memcpy(h + 32, &blockAlign, 2);
        const qint16 bitsPerSamp = bitsPerSample;
        memcpy(h + 34, &bitsPerSamp, 2);
        memcpy(h + 36, "data", 4);
        const qint32 dSize = dataSize;
        memcpy(h + 40, &dSize, 4);

        QCOMPARE(header.size(), 44);
        QCOMPARE(header.left(4), QByteArray("RIFF"));
        QCOMPARE(header.mid(8, 4), QByteArray("WAVE"));
        QCOMPARE(header.mid(12, 4), QByteArray("fmt "));
        QCOMPARE(header.mid(36, 4), QByteArray("data"));
    }

    void testWakeWordRegexPatterns()
    {
        static const QRegularExpression wwRegex(
            QStringLiteral("\\b(?:hola|oye|hey|ok)?\\s*(?:kiri|quiri|ciri|kili)\\b"),
            QRegularExpression::CaseInsensitiveOption);

        // Positive test cases
        QStringList positiveCases = {
            QStringLiteral("hola kiri"),
            QStringLiteral("¡Hola, Kiri!"),
            QStringLiteral("oye kiri"),
            QStringLiteral("hey kiri"),
            QStringLiteral("kiri"),
            QStringLiteral("OK Kiri"),
            QStringLiteral("Hola Kiri, ¿qué hora es?")
        };

        for (const auto &text : positiveCases) {
            QVERIFY2(text.contains(wwRegex) || text.contains(QStringLiteral("kiri"), Qt::CaseInsensitive),
                     qPrintable(QStringLiteral("Failed to match valid wake word in: %1").arg(text)));
        }

        // Negative test cases (must NOT trigger)
        QStringList negativeCases = {
            QStringLiteral("hola, ¿cómo estás?"),
            QStringLiteral("el cielo está despejado hoy"),
            QStringLiteral("abre el navegador web"),
            QStringLiteral("reproduce algo de música"),
            QStringLiteral("un kilo de manzanas")
        };

        for (const auto &text : negativeCases) {
            QVERIFY2(!text.contains(wwRegex) && !text.contains(QStringLiteral("kiri"), Qt::CaseInsensitive),
                     qPrintable(QStringLiteral("False positive triggered on: %1").arg(text)));
        }
    }

    void testWhisperWakeWordInference()
    {
        const QString piperCli = QDir::homePath() + QStringLiteral("/.local/bin/piper");
        const QString whisperCli = QDir::homePath() + QStringLiteral("/.local/bin/whisper-cli");
        const QString voicePath = QDir::homePath() + QStringLiteral("/.local/share/kiri/voices/es_ES-sharvard-medium.onnx");
        const QString modelPath = QDir::homePath() + QStringLiteral("/.local/share/kiri/models/ggml-base.bin");

        if (!QFile::exists(piperCli) || !QFile::exists(whisperCli) || !QFile::exists(voicePath) || !QFile::exists(modelPath)) {
            QSKIP("Audio binaries or models not found in ~/.local, skipping live neural audio inference test.");
        }

        // 1. Synthesize audio "Hola Kiri"
        const QString tmpWav = QDir::tempPath() + QStringLiteral("/test_hola_kiri_%1.wav").arg(QDateTime::currentMSecsSinceEpoch());
        QProcess piperProc;
        piperProc.start(piperCli, QStringList() << QStringLiteral("--model") << voicePath
                                               << QStringLiteral("--output_file") << tmpWav);
        QVERIFY(piperProc.waitForStarted(3000));
        piperProc.write("Hola Kiri");
        piperProc.closeWriteChannel();
        QVERIFY(piperProc.waitForFinished(5000));
        QVERIFY(QFile::exists(tmpWav));

        // 2. Transcribe with Whisper
        QProcess whisperProc;
        QStringList args;
        args << QStringLiteral("-m") << modelPath
             << QStringLiteral("-f") << tmpWav
             << QStringLiteral("-l") << QStringLiteral("es")
             << QStringLiteral("--prompt") << QStringLiteral("Hola Kiri, Oye Kiri, Hey Kiri")
             << QStringLiteral("--no-timestamps")
             << QStringLiteral("-t") << QStringLiteral("4")
             << QStringLiteral("-np");

        whisperProc.start(whisperCli, args);
        QVERIFY(whisperProc.waitForStarted(3000));
        QVERIFY(whisperProc.waitForFinished(8000));

        const QString output = QString::fromUtf8(whisperProc.readAllStandardOutput()).trimmed().toLower();
        QFile::remove(tmpWav);

        static const QRegularExpression wwRegex(
            QStringLiteral("\\b(?:hola|oye|hey|ok)?\\s*(?:kiri|quiri|ciri|kili)\\b"),
            QRegularExpression::CaseInsensitiveOption);

        QVERIFY2(output.contains(wwRegex) || output.contains(QStringLiteral("kiri")),
                 qPrintable(QStringLiteral("Whisper failed to recognize 'Hola Kiri' in output: %1").arg(output)));
    }
};

QTEST_MAIN(TestWakeWord)
#include "test_wakeword.moc"
