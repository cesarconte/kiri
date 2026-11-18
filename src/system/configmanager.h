/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace Kity {

class ConfigManager : public QObject
{
    Q_OBJECT

public:
    static ConfigManager &instance();

    // Generic readers
    QString readString(const QString &key, const QString &def = QString()) const;
    int readInt(const QString &key, int def = 0) const;
    double readDouble(const QString &key, double def = 0.0) const;
    bool readBool(const QString &key, bool def = false) const;

    // Generic writer
    void writeEntry(const QString &key, const QVariant &value);

    // Strongly-typed config getters & setters
    QString ollamaUrl() const;
    QString ollamaModel() const;
    void setOllamaModel(const QString &model);

    QString visionModel() const;
    QString geminiApiKey() const;
    QString geminiModel() const;
    QString systemPrompt() const;
    QString sttEngine() const;

    QString whisperModelPath() const;
    QString whisperLanguage() const;
    QString piperVoicePath() const;

    QString speechOutputMode() const;
    void setSpeechOutputMode(const QString &mode);

    QString panelIconStyle() const;
    void setPanelIconStyle(const QString &style);

    bool vadEnabled() const;
    void setVadEnabled(bool enabled);

    bool earconsEnabled() const;
    void setEarconsEnabled(bool enabled);

    bool useCentralizedVoice() const;
    void setUseCentralizedVoice(bool enabled);

    bool wakeWordEnabled() const;
    void setWakeWordEnabled(bool enabled);

    QString siriMode() const;
    QString aiBackend() const;
    bool knowledgeBaseEnabled() const;
    int knowledgeBaseMaxSnippets() const;
    QString defaultEmailClient() const;

    void reload();

Q_SIGNALS:
    void configChanged();

private:
    explicit ConfigManager(QObject *parent = nullptr);
    ~ConfigManager() override = default;
    ConfigManager(const ConfigManager &) = delete;
    ConfigManager &operator=(const ConfigManager &) = delete;
};

} // namespace Kity
