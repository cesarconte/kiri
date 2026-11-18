/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace Kity {

struct ContextResult {
    QString context;
    QString effectivePrompt;
    QString pendingCardType;
    QVariantMap pendingCardData;
    bool handledEarly = false;
    QString earlyAssistantResponse;
};

class ContextManager : public QObject
{
    Q_OBJECT

public:
    static ContextManager &instance();

    ContextResult gatherContext(const QString &userQuery);

    QString fetchWeatherContext(const QString &query, QString *cardTypeOut = nullptr, QVariantMap *cardDataOut = nullptr) const;
    QString fetchWebSearchContext(const QString &query) const;
    QString fetchDynamicLocalContext(const QString &query, const QString &intent = QStringLiteral("auto")) const;
    QString fetchKnowledgeBaseContext(const QString &query) const;
    QString collectKWinWindowsInfo() const;
    QString collectRecentNotificationsContext() const;

private:
    explicit ContextManager(QObject *parent = nullptr);
    ~ContextManager() override = default;
    ContextManager(const ContextManager &) = delete;
    ContextManager &operator=(const ContextManager &) = delete;
};

} // namespace Kity
