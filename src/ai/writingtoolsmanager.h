/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace Kity {

class WritingToolsManager : public QObject
{
    Q_OBJECT

public:
    static WritingToolsManager &instance();

    QString buildPrompt(const QString &action, const QString &sourceText) const;
    QString languageDisplayName(const QString &code) const;
    QStringList evaluateMath(const QString &text) const;
    QString renderMarkdown(const QString &markdown) const;

private:
    explicit WritingToolsManager(QObject *parent = nullptr);
    ~WritingToolsManager() override = default;

    WritingToolsManager(const WritingToolsManager &) = delete;
    WritingToolsManager &operator=(const WritingToolsManager &) = delete;
};

} // namespace Kity
