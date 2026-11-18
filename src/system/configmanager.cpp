/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "configmanager.h"

#include <KSharedConfig>
#include <KConfigGroup>
#include <QFile>
#include <QStandardPaths>

namespace Kity {

ConfigManager &ConfigManager::instance()
{
    static ConfigManager s_instance;
    return s_instance;
}

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
{
}

QString ConfigManager::readString(const QString &key, const QString &def) const
{
    const KSharedConfig::Ptr kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    if (kityrc->hasGroup(QStringLiteral("General")) && kityrc->group(QStringLiteral("General")).hasKey(key)) {
        return kityrc->group(QStringLiteral("General")).readEntry(key, def);
    }
    return def;
}

int ConfigManager::readInt(const QString &key, int def) const
{
    const KSharedConfig::Ptr kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    if (kityrc->hasGroup(QStringLiteral("General")) && kityrc->group(QStringLiteral("General")).hasKey(key)) {
        return kityrc->group(QStringLiteral("General")).readEntry(key, def);
    }
    return def;
}

double ConfigManager::readDouble(const QString &key, double def) const
{
    const KSharedConfig::Ptr kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    if (kityrc->hasGroup(QStringLiteral("General")) && kityrc->group(QStringLiteral("General")).hasKey(key)) {
        return kityrc->group(QStringLiteral("General")).readEntry(key, def);
    }
    return def;
}

bool ConfigManager::readBool(const QString &key, bool def) const
{
    const KSharedConfig::Ptr kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    if (kityrc->hasGroup(QStringLiteral("General")) && kityrc->group(QStringLiteral("General")).hasKey(key)) {
        return kityrc->group(QStringLiteral("General")).readEntry(key, def);
    }
    return def;
}

void ConfigManager::writeEntry(const QString &key, const QVariant &value)
{
    auto kityrc = KSharedConfig::openConfig(QStringLiteral("kityrc"));
    kityrc->group(QStringLiteral("General")).writeEntry(key, value);
    kityrc->sync();
    Q_EMIT configChanged();
}

QString ConfigManager::ollamaUrl() const
{
    QString url = readString(QStringLiteral("ollamaUrl"), QStringLiteral("http://127.0.0.1:11434"));
    while (url.endsWith(QLatin1Char('/'))) {
        url.chop(1);
    }
    return url;
}

QString ConfigManager::ollamaModel() const
{
    return readString(QStringLiteral("ollamaModel"), QStringLiteral("llama3.2:3b"));
}

void ConfigManager::setOllamaModel(const QString &model)
{
    if (ollamaModel() != model) {
        writeEntry(QStringLiteral("ollamaModel"), model);
    }
}

QString ConfigManager::visionModel() const
{
    return readString(QStringLiteral("ollamaVisionModel"), ollamaModel());
}

QString ConfigManager::geminiApiKey() const
{
    QString key = readString(QStringLiteral("geminiApiKey")).trimmed();
    if (key.isEmpty()) {
        key = readString(QStringLiteral("googleApiKey")).trimmed();
    }
    if (key.isEmpty()) {
        key = qEnvironmentVariable("GEMINI_API_KEY").trimmed();
    }
    return key;
}

QString ConfigManager::geminiModel() const
{
    return readString(QStringLiteral("geminiModel"), QStringLiteral("gemini-2.0-flash")).trimmed();
}

QString ConfigManager::systemPrompt() const
{
    QString basePrompt = readString(QStringLiteral("systemPrompt"),
        QStringLiteral("Eres Kity, un asistente de voz e inteligencia artificial nativo para KDE Plasma en CachyOS Linux. "
                       "Responde SIEMPRE en español de España (castellano), de forma concisa, clara, directa y natural, optimizada para síntesis de voz (TTS). "
                       "Al referirte a cantidades, temperaturas o unidades métricas, exprésalas con palabras naturales y completas para locución hablada (por ejemplo '15 grados' o '150 mililitros')."));
    const QString mode = siriMode();
    if (mode == QLatin1String("classic")) {
        basePrompt += QStringLiteral("\n\n[MODO KITY CLASSIC]: Responde SIEMPRE en una o dos frases muy breves y directas (máximo 30 palabras), sin encabezados ni listas largas, al estilo del asistente clásico de voz.");
    }
    return basePrompt;
}

QString ConfigManager::sttEngine() const
{
    return readString(QStringLiteral("sttEngine"), QStringLiteral("gemini")).toLower().trimmed();
}

QString ConfigManager::whisperModelPath() const
{
    QString path = readString(QStringLiteral("whisperModelPath"));
    if (!path.isEmpty() && QFile::exists(path)) {
        return path;
    }
    const QString smallLocated = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("kity/models/ggml-small.bin"));
    if (!smallLocated.isEmpty()) {
        return smallLocated;
    }
    const QString baseLocated = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("kity/models/ggml-base.bin"));
    if (!baseLocated.isEmpty()) {
        return baseLocated;
    }
    const QString sysSmallPath = QStringLiteral("/usr/share/kity/models/ggml-small.bin");
    if (QFile::exists(sysSmallPath)) {
        return sysSmallPath;
    }
    const QString sysBasePath = QStringLiteral("/usr/share/kity/models/ggml-base.bin");
    if (QFile::exists(sysBasePath)) {
        return sysBasePath;
    }
    return QString();
}

QString ConfigManager::whisperLanguage() const
{
    const QString lang = readString(QStringLiteral("whisperLanguage"), QStringLiteral("es")).trimmed().toLower();
    if (lang.isEmpty() || lang == QStringLiteral("auto")) {
        return QStringLiteral("es");
    }
    return lang;
}

QString ConfigManager::piperVoicePath() const
{
    QString path = readString(QStringLiteral("piperVoicePath"));
    if (!path.isEmpty() && QFile::exists(path)) {
        return path;
    }
    const QString located = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("kity/voices/es_ES-sharvard-medium.onnx"));
    if (!located.isEmpty()) {
        return located;
    }
    const QString sysVoice = QStringLiteral("/usr/share/kity/voices/es_ES-sharvard-medium.onnx");
    if (QFile::exists(sysVoice)) {
        return sysVoice;
    }
    return QString();
}

QString ConfigManager::speechOutputMode() const
{
    return readString(QStringLiteral("speechOutputMode"), QStringLiteral("always"));
}

void ConfigManager::setSpeechOutputMode(const QString &mode)
{
    writeEntry(QStringLiteral("speechOutputMode"), mode);
}

QString ConfigManager::panelIconStyle() const
{
    return readString(QStringLiteral("panelIconStyle"), QStringLiteral("symbolic"));
}

void ConfigManager::setPanelIconStyle(const QString &style)
{
    writeEntry(QStringLiteral("panelIconStyle"), style);
}

bool ConfigManager::vadEnabled() const
{
    return readBool(QStringLiteral("vadEnabled"), true);
}

void ConfigManager::setVadEnabled(bool enabled)
{
    writeEntry(QStringLiteral("vadEnabled"), enabled);
}

bool ConfigManager::earconsEnabled() const
{
    return readBool(QStringLiteral("earconsEnabled"), true);
}

void ConfigManager::setEarconsEnabled(bool enabled)
{
    writeEntry(QStringLiteral("earconsEnabled"), enabled);
}

bool ConfigManager::useCentralizedVoice() const
{
    return readBool(QStringLiteral("useCentralizedVoice"), true);
}

void ConfigManager::setUseCentralizedVoice(bool enabled)
{
    writeEntry(QStringLiteral("useCentralizedVoice"), enabled);
}

bool ConfigManager::wakeWordEnabled() const
{
    return readBool(QStringLiteral("wakeWordEnabled"), true);
}

void ConfigManager::setWakeWordEnabled(bool enabled)
{
    writeEntry(QStringLiteral("wakeWordEnabled"), enabled);
}

QString ConfigManager::siriMode() const
{
    return readString(QStringLiteral("siriMode"), QStringLiteral("ai")).toLower();
}

QString ConfigManager::aiBackend() const
{
    return readString(QStringLiteral("aiBackend"), QStringLiteral("hybrid")).toLower();
}

bool ConfigManager::knowledgeBaseEnabled() const
{
    return readBool(QStringLiteral("knowledgeBaseEnabled"), true);
}

int ConfigManager::knowledgeBaseMaxSnippets() const
{
    return readInt(QStringLiteral("knowledgeBaseMaxSnippets"), 3);
}

QString ConfigManager::defaultEmailClient() const
{
    return readString(QStringLiteral("defaultEmailClient"), QStringLiteral("proton"));
}

void ConfigManager::reload()
{
    KSharedConfig::openConfig(QStringLiteral("kityrc"))->reparseConfiguration();
    Q_EMIT configChanged();
}

} // namespace Kity
