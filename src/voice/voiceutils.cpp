/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "voiceutils.h"

#include <QDataStream>
#include <QIODevice>
#include <QRegularExpression>
#include <QStringList>

namespace Kity {

QString VoiceUtils::cleanWhisperOutput(const QString &raw)
{
    QStringList keptLines;
    static const QRegularExpression timestampRe(
        QStringLiteral(R"(^\s*\[[0-9:.]+\s*-->\s*[0-9:.]+\]\s*)"));
    static const QRegularExpression bracketTagRe(
        QStringLiteral(R"(\[[^\]]*\])"));
    static const QRegularExpression parenTagRe(
        QStringLiteral(R"(\([^\)]*\))"));

    const QStringList lines = raw.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line.remove(timestampRe);
        line.remove(bracketTagRe);
        line.remove(parenTagRe);
        line = line.trimmed();
        if (!line.isEmpty()) {
            keptLines.append(line);
        }
    }
    QString res = keptLines.join(QLatin1Char(' ')).simplified();
    static const QRegularExpression jijonRe(QStringLiteral(R"(\b[Jj]ij[oó]n\b)"));
    res.replace(jijonRe, QStringLiteral("Gijón"));
    return res;
}

QString VoiceUtils::removeFillerWords(const QString &text, const QString &lang)
{
    if (text.trimmed().isEmpty()) {
        return QString();
    }

    QString out = text;

    if (lang.startsWith(QLatin1String("es"), Qt::CaseInsensitive)) {
        // 1. Eliminar repetición accidental de palabras contiguas (tartamudeo: "el el", "vamos vamos")
        static const QRegularExpression stutterRe(
            QStringLiteral(R"(\b([a-zA-ZáéíóúüñÁÉÍÓÚÜÑ]+)\s+\1\b)"),
            QRegularExpression::CaseInsensitiveOption);
        out.replace(stutterRe, QStringLiteral("\\1"));

        // 2. Muletillas y sonidos de titubeo (eh, em, este, mmm, ah)
        static const QRegularExpression hesitationRe(
            QStringLiteral(R"(\b(?:eh+|em+|este+|mmm+|ah+)\b)"),
            QRegularExpression::CaseInsensitiveOption);
        out.replace(hesitationRe, QStringLiteral(""));

        // 3. Muletillas conversacionales al inicio de frase o encadenadas
        static const QRegularExpression starterFillerRe(
            QStringLiteral(R"(^(?:\s*(?:bueno|pues|o sea|en plan|digamos que)[,\s]*)+)"),
            QRegularExpression::CaseInsensitiveOption);
        out.replace(starterFillerRe, QStringLiteral(""));

        static const QRegularExpression inlineFillerRe(
            QStringLiteral(R"((?:,\s*|\s+)(?:o sea|en plan)(?:,\s*|\s+))"),
            QRegularExpression::CaseInsensitiveOption);
        out.replace(inlineFillerRe, QStringLiteral(", "));
    } else if (lang.startsWith(QLatin1String("en"), Qt::CaseInsensitive)) {
        static const QRegularExpression stutterRe(
            QStringLiteral(R"(\b([a-zA-Z]+)\s+\1\b)"),
            QRegularExpression::CaseInsensitiveOption);
        out.replace(stutterRe, QStringLiteral("\\1"));

        static const QRegularExpression hesitationRe(
            QStringLiteral(R"(\b(?:uh+|um+|er+|ah+|like,?\s+|you know,?\s+)\b)"),
            QRegularExpression::CaseInsensitiveOption);
        out.replace(hesitationRe, QStringLiteral(""));
    }

    // Normalizar signos y espacios residuales
    static const QRegularExpression orphanLeadingPunctRe(QStringLiteral(R"(^[,\s;:.-]+)"));
    out.remove(orphanLeadingPunctRe);
    static const QRegularExpression multiSpaceRe(QStringLiteral(R"([ \t]{2,})"));
    out.replace(multiSpaceRe, QStringLiteral(" "));
    static const QRegularExpression spaceBeforePunctRe(QStringLiteral(R"( +([,.;:?!]))"));
    out.replace(spaceBeforePunctRe, QStringLiteral("\\1"));
    static const QRegularExpression multiCommaRe(QStringLiteral(R"(,{2,})"));
    out.replace(multiCommaRe, QStringLiteral(","));

    out = out.trimmed();
    if (!out.isEmpty()) {
        out[0] = out[0].toUpper();
    }

    return out;
}

QString VoiceUtils::stripMarkdownForSpeech(const QString &markdown)
{
    QString clean = markdown;

    // Eliminar bloques de código ```...```
    static const QRegularExpression codeBlockRe(QStringLiteral(R"(```[\s\S]*?```)"));
    clean.remove(codeBlockRe);

    // Eliminar código inline `...`
    static const QRegularExpression inlineCodeRe(QStringLiteral(R"(`([^`]+)`)"));
    clean.replace(inlineCodeRe, QStringLiteral("\\1"));

    // Eliminar encabezados Markdown (#, ##, ###)
    static const QRegularExpression headersRe(QStringLiteral(R"(^#{1,6}\s+)"), QRegularExpression::MultilineOption);
    clean.remove(headersRe);

    // Eliminar negrita y cursiva (**texto**, *texto*, __texto__, _texto_)
    static const QRegularExpression boldItalicRe(QStringLiteral(R"([*_]{1,3}([^*_]+)[*_]{1,3})"));
    clean.replace(boldItalicRe, QStringLiteral("\\1"));

    // Convertir enlaces [texto](url) -> texto
    static const QRegularExpression linksRe(QStringLiteral(R"(\[([^\]]+)\]\([^)]+\))"));
    clean.replace(linksRe, QStringLiteral("\\1"));

    // Eliminar imágenes ![alt](url)
    static const QRegularExpression imagesRe(QStringLiteral(R"(!\[[^\]]*\]\([^)]+\))"));
    clean.remove(imagesRe);

    // Eliminar listas viñetas (* item, - item, + item)
    static const QRegularExpression bulletsRe(QStringLiteral(R"(^\s*[-*+]\s+)"), QRegularExpression::MultilineOption);
    clean.replace(bulletsRe, QStringLiteral(", "));

    // Eliminar listas numeradas (1. item)
    static const QRegularExpression numberedListRe(QStringLiteral(R"(^\s*\d+\.\s+)"), QRegularExpression::MultilineOption);
    clean.replace(numberedListRe, QStringLiteral(", "));

    // Eliminar citas (> cita)
    static const QRegularExpression blockquoteRe(QStringLiteral(R"(^\s*>\s+)"), QRegularExpression::MultilineOption);
    clean.remove(blockquoteRe);

    // Eliminar separadores horizontales (---, ***, ___)
    static const QRegularExpression hrRe(QStringLiteral(R"(^\s*[-*_]{3,}\s*$)"), QRegularExpression::MultilineOption);
    clean.remove(hrRe);

    // Normalizar saltos de línea y espacios
    clean.replace(QLatin1Char('\n'), QLatin1Char(' '));
    static const QRegularExpression multiSpace(QStringLiteral(R"(\s+)"));
    clean = clean.replace(multiSpace, QStringLiteral(" ")).trimmed();

    return clean;
}

QByteArray VoiceUtils::createWavHeader(int dataSize, int sampleRate, int channels, int bitsPerSample)
{
    QByteArray header;
    QDataStream stream(&header, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    const int byteRate = sampleRate * channels * (bitsPerSample / 8);
    const int blockAlign = channels * (bitsPerSample / 8);
    const int chunkSize = 36 + dataSize;

    // RIFF chunk
    header.append("RIFF", 4);
    stream << quint32(chunkSize);
    header.append("WAVE", 4);

    // fmt subchunk
    header.append("fmt ", 4);
    stream << quint32(16);             // Subchunk1Size (16 for PCM)
    stream << quint16(1);              // AudioFormat (1 for PCM)
    stream << quint16(channels);
    stream << quint32(sampleRate);
    stream << quint32(byteRate);
    stream << quint16(blockAlign);
    stream << quint16(bitsPerSample);

    // data subchunk
    header.append("data", 4);
    stream << quint32(dataSize);

    return header;
}

} // namespace Kity
