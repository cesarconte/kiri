/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "websearchmanager.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>

namespace Kity {

QJsonObject SearchResultItem::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("title")] = title;
    obj[QStringLiteral("url")] = url;
    obj[QStringLiteral("snippet")] = snippet;
    if (!deepContent.isEmpty()) {
        obj[QStringLiteral("deepContent")] = deepContent;
    }
    return obj;
}

WebSearchManager &WebSearchManager::instance()
{
    static WebSearchManager s_instance;
    return s_instance;
}

WebSearchManager::WebSearchManager(QObject *parent)
    : QObject(parent)
{
}

QByteArray WebSearchManager::httpGetSync(const QUrl &url, int timeoutMs) const
{
    QNetworkAccessManager mgr;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"));
    req.setRawHeader("Accept-Language", "es-ES,es;q=0.9,en;q=0.8");

    QNetworkReply *reply = mgr.get(req);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    timer.start(timeoutMs);
    loop.exec();

    QByteArray data;
    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        data = reply->readAll();
    }
    reply->deleteLater();
    return data;
}

QByteArray WebSearchManager::httpPostFormSync(const QUrl &url, const QByteArray &formData, int timeoutMs) const
{
    QNetworkAccessManager mgr;
    mgr.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"));
    req.setRawHeader("Accept-Language", "es-ES,es;q=0.9,en;q=0.8");
    req.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");

    QNetworkReply *reply = mgr.post(req, formData);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    timer.start(timeoutMs);
    loop.exec();

    QByteArray data;
    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        data = reply->readAll();
    }
    reply->deleteLater();
    return data;
}

QString WebSearchManager::extractCleanArticleText(const QString &html) const
{
    if (html.isEmpty()) {
        return QString();
    }

    QString text = html;

    // 1. Strip script, style, svg, iframe, noscript, nav, header, footer, aside blocks
    static const QRegularExpression stripBlockRegex(
        QStringLiteral("<(script|style|svg|iframe|noscript|nav|header|footer|aside)[^>]*>.*?</\\1>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    text.remove(stripBlockRegex);

    // 2. Convert block boundaries to newlines
    static const QRegularExpression blockBreaks(
        QStringLiteral("<(?:br|p|div|li|h[1-6]|tr)[^>]*>"),
        QRegularExpression::CaseInsensitiveOption);
    text.replace(blockBreaks, QStringLiteral("\n"));

    // 3. Remove all remaining HTML tags
    static const QRegularExpression anyTag(QStringLiteral("<[^>]+>"));
    text.remove(anyTag);

    // 4. Decode HTML entities
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""))
        .replace(QStringLiteral("&#x27;"), QStringLiteral("'"))
        .replace(QStringLiteral("&#39;"), QStringLiteral("'"))
        .replace(QStringLiteral("&amp;"), QStringLiteral("&"))
        .replace(QStringLiteral("&lt;"), QStringLiteral("<"))
        .replace(QStringLiteral("&gt;"), QStringLiteral(">"))
        .replace(QStringLiteral("&nbsp;"), QStringLiteral(" "))
        .replace(QStringLiteral("&#160;"), QStringLiteral(" "));

    // 5. Keep only substantive paragraphs (>= 35 characters)
    const QStringList rawLines = text.split(QLatin1Char('\n'));
    QStringList substantiveLines;
    for (const QString &line : rawLines) {
        const QString trimmed = line.simplified();
        if (trimmed.length() >= 35) {
            substantiveLines.append(trimmed);
        }
    }

    QString result = substantiveLines.join(QStringLiteral("\n\n"));
    if (result.length() > 2500) {
        result = result.left(2500) + QStringLiteral("…");
    }
    return result;
}

QString WebSearchManager::fetchDeepWebPageContent(const QUrl &url) const
{
    if (!url.isValid() || url.scheme().isEmpty()) {
        return QString();
    }
    const QByteArray htmlBytes = httpGetSync(url, 2800);
    if (htmlBytes.isEmpty()) {
        return QString();
    }
    return extractCleanArticleText(QString::fromUtf8(htmlBytes));
}

QList<SearchResultItem> WebSearchManager::search(const QString &query, int maxResults) const
{
    QString searchTerm = query.trimmed();
    static const QRegularExpression prefixStrip(
        QStringLiteral("^(?:busca(?:r)?|averigua(?:r)?|qui[eé]n es|qu[eé] es|dime|encuentra(?:r)?|consulta(?:r)?)\\s+"),
        QRegularExpression::CaseInsensitiveOption);
    searchTerm.remove(prefixStrip);
    searchTerm = searchTerm.trimmed();
    if (searchTerm.isEmpty()) {
        return {};
    }

    QList<SearchResultItem> foundItems;

    // 1. DuckDuckGo HTML Search via POST (reliable, real-time web results with links & snippets)
    const QUrl ddgUrl(QStringLiteral("https://html.duckduckgo.com/html/"));
    const QByteArray formData = "q=" + QUrl::toPercentEncoding(searchTerm);
    const QByteArray ddgResp = httpPostFormSync(ddgUrl, formData, 3500);

    if (!ddgResp.isEmpty()) {
        const QString html = QString::fromUtf8(ddgResp);

        static const QRegularExpression titleLinkRx(
            QStringLiteral("<h2 class=\"result__title\">\\s*<a[^>]*class=\"result__a\"[^>]*href=\"([^\"]+)\"[^>]*>(.*?)</a>"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression snippetRx(
            QStringLiteral("<a class=\"result__snippet\"[^>]*>(.*?)</a>"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression tagRx(QStringLiteral("<[^>]+>"));

        auto titleIt = titleLinkRx.globalMatch(html);
        auto snippetIt = snippetRx.globalMatch(html);

        while (titleIt.hasNext() && foundItems.size() < maxResults) {
            auto titleMatch = titleIt.next();
            QString rawUrl = titleMatch.captured(1);
            QString rawTitle = titleMatch.captured(2);

            // Clean title
            rawTitle.remove(tagRx);
            rawTitle.replace(QStringLiteral("&quot;"), QStringLiteral("\""))
                    .replace(QStringLiteral("&#x27;"), QStringLiteral("'"))
                    .replace(QStringLiteral("&amp;"), QStringLiteral("&"))
                    .replace(QStringLiteral("&lt;"), QStringLiteral("<"))
                    .replace(QStringLiteral("&gt;"), QStringLiteral(">"));
            const QString title = rawTitle.simplified().trimmed();

            // Decode URL if it was wrapped in /l/?uddg=
            QString cleanUrl = rawUrl;
            if (rawUrl.contains(QStringLiteral("uddg="))) {
                QUrl parsed(rawUrl.startsWith(QLatin1String("//")) ? QStringLiteral("https:") + rawUrl : rawUrl);
                const QString uddg = QUrlQuery(parsed).queryItemValue(QStringLiteral("uddg"));
                if (!uddg.isEmpty()) {
                    cleanUrl = QUrl::fromPercentEncoding(uddg.toUtf8());
                }
            } else if (rawUrl.startsWith(QLatin1String("//"))) {
                cleanUrl = QStringLiteral("https:") + rawUrl;
            }

            // Extract snippet
            QString snippet;
            if (snippetIt.hasNext()) {
                snippet = snippetIt.next().captured(1);
                snippet.remove(tagRx);
                snippet.replace(QStringLiteral("&quot;"), QStringLiteral("\""))
                       .replace(QStringLiteral("&#x27;"), QStringLiteral("'"))
                       .replace(QStringLiteral("&amp;"), QStringLiteral("&"))
                       .replace(QStringLiteral("&lt;"), QStringLiteral("<"))
                       .replace(QStringLiteral("&gt;"), QStringLiteral(">"));
                snippet = snippet.simplified().trimmed();
            }

            if (!cleanUrl.isEmpty() && !title.isEmpty()) {
                SearchResultItem item;
                item.title = title;
                item.url = cleanUrl;
                item.snippet = snippet;
                foundItems.append(item);
            }
        }
    }

    // 2. Fallback: Wikipedia OpenSearch API
    if (foundItems.isEmpty()) {
        const QUrl wikiUrl(QStringLiteral("https://es.wikipedia.org/w/api.php?action=opensearch&search=%1&limit=2&namespace=0&format=json")
                               .arg(QString::fromUtf8(QUrl::toPercentEncoding(searchTerm))));
        const QByteArray wikiResp = httpGetSync(wikiUrl, 2500);
        if (!wikiResp.isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(wikiResp);
            if (doc.isArray()) {
                const QJsonArray arr = doc.array();
                if (arr.size() >= 4) {
                    const QJsonArray titles = arr.at(1).toArray();
                    const QJsonArray descs = arr.at(2).toArray();
                    const QJsonArray links = arr.at(3).toArray();
                    for (int i = 0; i < titles.size() && foundItems.size() < maxResults; ++i) {
                        const QString t = titles.at(i).toString();
                        const QString d = descs.at(i).toString();
                        const QString l = links.at(i).toString();
                        if (!d.isEmpty()) {
                            SearchResultItem item;
                            item.title = t;
                            item.url = l;
                            item.snippet = d;
                            foundItems.append(item);
                        }
                    }
                }
            }
        }
    }

    return foundItems;
}

QString WebSearchManager::fetchWebSearchContext(const QString &query) const
{
    QString searchTerm = query.trimmed();
    static const QRegularExpression prefixStrip(
        QStringLiteral("^(?:busca(?:r)?|averigua(?:r)?|qui[eé]n es|qu[eé] es|dime|encuentra(?:r)?|consulta(?:r)?)\\s+"),
        QRegularExpression::CaseInsensitiveOption);
    searchTerm.remove(prefixStrip);
    searchTerm = searchTerm.trimmed();
    if (searchTerm.isEmpty()) {
        return QString();
    }

    QList<SearchResultItem> foundItems = search(searchTerm, 4);
    if (foundItems.isEmpty()) {
        return QString();
    }

    // Deep Web Research: Fetch actual page body for the top 2 authoritative sources
    for (int i = 0; i < qMin(foundItems.size(), 2); ++i) {
        const QString deepText = fetchDeepWebPageContent(QUrl(foundItems[i].url));
        if (deepText.length() >= 200) {
            foundItems[i].deepContent = deepText;
        }
    }

    // Format structured research context with citations
    QStringList formattedSources;
    QStringList sourcesListForFooter;
    int idx = 1;
    for (const auto &item : foundItems) {
        if (!item.deepContent.isEmpty()) {
            formattedSources.append(QStringLiteral(
                "Fuente [%1]: \"%2\"\n"
                "Enlace: %3\n"
                "Extracto completo del artículo:\n\"\"\"\n%4\n\"\"\"")
                .arg(QString::number(idx), item.title, item.url, item.deepContent));
        } else {
            formattedSources.append(QStringLiteral(
                "Fuente [%1]: \"%2\"\n"
                "Enlace: %3\n"
                "Resumen verificado: %4")
                .arg(QString::number(idx), item.title, item.url, item.snippet));
        }
        sourcesListForFooter.append(QStringLiteral("- [%1](%2)").arg(item.title, item.url));
        idx++;
    }

    return QStringLiteral(
        "[INVESTIGACIÓN WEB EN TIEMPO REAL (DEEP WEB RESEARCH) PARA: \"%1\"]\n\n"
        "%2\n\n"
        "INSTRUCCIONES OBLIGATORIAS PARA KITY:\n"
        "- Responde en español de España de forma concisa, analítica, natural y directa utilizando la información extraída de estas páginas.\n"
        "- Si mencionas un dato relevante, referencia su fuente entre corchetes con enlace Markdown (por ejemplo: [%3](%4) o [1](%4)).\n"
        "- Al final de tu respuesta, añade OBLIGATORIAMENTE este apartado de fuentes consultadas:\n\n"
        "🌐 **Fuentes consultadas:**\n%5")
        .arg(searchTerm, formattedSources.join(QStringLiteral("\n\n")), foundItems.first().title, foundItems.first().url, sourcesListForFooter.join(QLatin1Char('\n')));
}

} // namespace Kity
