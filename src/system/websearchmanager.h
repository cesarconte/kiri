/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QJsonObject>

namespace Kity {

struct SearchResultItem {
    QString title;
    QString url;
    QString snippet;
    QString deepContent;

    QJsonObject toJson() const;
};

/**
 * @brief WebSearchManager encapsulates real-time Internet search (DuckDuckGo, Wikipedia),
 * deep article content retrieval, and HTML sanitization/cleaning for LLM context injection.
 */
class WebSearchManager : public QObject
{
    Q_OBJECT

public:
    static WebSearchManager &instance();

    // Core web search & article extraction
    QList<SearchResultItem> search(const QString &query, int maxResults = 4) const;
    QString extractCleanArticleText(const QString &html) const;
    QString fetchDeepWebPageContent(const QUrl &url) const;

    // Full research context formatting (for prompt injection or tool execution)
    QString fetchWebSearchContext(const QString &query) const;

private:
    explicit WebSearchManager(QObject *parent = nullptr);
    ~WebSearchManager() override = default;

    QByteArray httpGetSync(const QUrl &url, int timeoutMs = 3000) const;
    QByteArray httpPostFormSync(const QUrl &url, const QByteArray &formData, int timeoutMs = 3500) const;

    WebSearchManager(const WebSearchManager &) = delete;
    WebSearchManager &operator=(const WebSearchManager &) = delete;
};

} // namespace Kity
