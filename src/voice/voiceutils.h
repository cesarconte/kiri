/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QString>

namespace Kity {

class VoiceUtils
{
public:
    static QString cleanWhisperOutput(const QString &raw);
    static QString removeFillerWords(const QString &text, const QString &lang = QStringLiteral("es"));
    static QString stripMarkdownForSpeech(const QString &markdown);
    static QByteArray createWavHeader(int dataSize, int sampleRate, int channels, int bitsPerSample);
};

} // namespace Kity
