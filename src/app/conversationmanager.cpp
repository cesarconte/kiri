/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "conversationmanager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

namespace Kity {

ConversationManager::ConversationManager(QObject *parent)
    : QObject(parent)
{
}

QVariantList ConversationManager::loadConversations(const QString &historyRetention, const QString &openToMode, QString &activeIdOut, QVariantList &activeMessagesOut)
{
    QVariantList convList;
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity");
    QDir().mkpath(dataDir);
    const QString convFile = dataDir + QStringLiteral("/conversations.json");
    QFile f(convFile);
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isArray()) {
            convList = doc.array().toVariantList();
        }
    }

    if (convList.isEmpty()) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        auto makeConv = [nowMs](const QString &id, const QString &timeLabel, const QString &title, const QString &preview, bool pinned, const QVariantList &msgs) {
            QVariantMap map;
            map[QStringLiteral("id")] = id;
            map[QStringLiteral("timeLabel")] = timeLabel;
            map[QStringLiteral("title")] = title;
            map[QStringLiteral("preview")] = preview;
            map[QStringLiteral("pinned")] = pinned;
            map[QStringLiteral("updatedAtMs")] = nowMs;
            map[QStringLiteral("messages")] = msgs;
            return map;
        };

        QVariantList siriAiMsgs;
        {
            QVariantMap u;
            u[QStringLiteral("role")] = QStringLiteral("user");
            u[QStringLiteral("content")] = QStringLiteral("Tell me what's new with Kity AI in Golden Gate");
            siriAiMsgs.append(u);

            QVariantMap a;
            a[QStringLiteral("role")] = QStringLiteral("assistant");
            a[QStringLiteral("content")] = QStringLiteral(
                "### Writing Tools\n"
                "Kity can now generate text drafts from scratch, shorten content, or turn bullet points into prose. "
                "It also provides automatic proofreading for spelling and grammar as you type across the system.\n\n"
                "### Dedicated Kity App\n"
                "For the first time, Kity has a standalone app that acts as a conversational chatbot with a full conversation sidebar, "
                "allowing you to look up past answers and continue older conversations with context intact.\n\n"
                "### Voice and Dictation\n"
                "The assistant features a brand-new voice experience that is more conversational and detailed. "
                "You can customize Kity's expressiveness and speaking pace to suit your preferences.");
            QVariantList sources;
            QVariantMap s1;
            s1[QStringLiteral("title")] = QStringLiteral("CachyOS Intelligence +2");
            s1[QStringLiteral("url")] = QStringLiteral("https://cachyos.org");
            sources.append(s1);
            a[QStringLiteral("sources")] = sources;
            siriAiMsgs.append(a);
        }

        convList.append(makeConv(
            QStringLiteral("conv-siri-ai"),
            QStringLiteral("7:05 AM"),
            QStringLiteral("Kity AI"),
            QStringLiteral("Kity recently introduced a completely rebuilt digital assistant powered by the next generation of local & cloud intelligence..."),
            true,
            siriAiMsgs));

        convList.append(makeConv(
            QStringLiteral("conv-features"),
            QStringLiteral("Sunday"),
            QStringLiteral("Golden Gate Features"),
            QStringLiteral("In macOS Golden Gate, Liquid Glass introduces a continuous opacity slider, edge-to-edge sidebars, and bouncy stoplights..."),
            false,
            QVariantList()));

        convList.append(makeConv(
            QStringLiteral("conv-notes"),
            QStringLiteral("Sunday"),
            QStringLiteral("Markdown in Notes"),
            QStringLiteral("I've exported your note into clean Markdown format with horizontal dividers..."),
            false,
            QVariantList()));

        convList.append(makeConv(
            QStringLiteral("conv-system"),
            QStringLiteral("Sunday"),
            QStringLiteral("System Status"),
            QStringLiteral("Kernel BORE and Wayland compositor running at < 1ms frame latency..."),
            false,
            QVariantList()));

        saveConversations(convList);
    }

    // Apply Conversation Retention policy ("forever", "1year", "30days")
    if (historyRetention == QLatin1String("30days") || historyRetention == QLatin1String("1year")) {
        const qint64 maxAgeMs = (historyRetention == QLatin1String("30days"))
            ? (30LL * 24LL * 3600LL * 1000LL)
            : (365LL * 24LL * 3600LL * 1000LL);
        const qint64 cutoffMs = QDateTime::currentMSecsSinceEpoch() - maxAgeMs;
        QVariantList filtered;
        for (const QVariant &v : std::as_const(convList)) {
            const QVariantMap map = v.toMap();
            const bool pinned = map.value(QStringLiteral("pinned")).toBool();
            const qint64 updatedMs = map.value(QStringLiteral("updatedAtMs"), QDateTime::currentMSecsSinceEpoch()).toLongLong();
            if (pinned || updatedMs >= cutoffMs) {
                filtered.append(map);
            }
        }
        if (filtered.size() != convList.size() && !filtered.isEmpty()) {
            convList = filtered;
            saveConversations(convList);
        }
    }

    if (openToMode == QLatin1String("new")) {
        activeIdOut = createNewConversation(convList);
        activeMessagesOut.clear();
        return convList;
    }

    if (!convList.isEmpty()) {
        const QVariantMap firstMap = convList.first().toMap();
        activeIdOut = firstMap.value(QStringLiteral("id")).toString();
        activeMessagesOut = firstMap.value(QStringLiteral("messages")).toList();
    }
    return convList;
}

void ConversationManager::saveConversations(const QVariantList &conversations)
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity");
    QDir().mkpath(dataDir);
    const QString convFile = dataDir + QStringLiteral("/conversations.json");
    QFile f(convFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(QJsonArray::fromVariantList(conversations)).toJson(QJsonDocument::Compact));
    }
}

void ConversationManager::syncActiveFromMessages(QVariantList &conversations, const QString &activeId, const QVariantList &messages)
{
    if (messages.isEmpty() || activeId.isEmpty()) {
        return;
    }

    QString firstUserPrompt;
    QString lastAssistantReply;
    for (const QVariant &v : std::as_const(messages)) {
        const QVariantMap msg = v.toMap();
        const QString role = msg.value(QStringLiteral("role")).toString();
        const QString content = msg.value(QStringLiteral("content")).toString().trimmed();
        if (role == QLatin1String("user") && firstUserPrompt.isEmpty()) {
            firstUserPrompt = content.left(32);
        } else if (role == QLatin1String("assistant") && !content.isEmpty()) {
            lastAssistantReply = content;
        }
    }

    for (int i = 0; i < conversations.size(); ++i) {
        QVariantMap map = conversations[i].toMap();
        if (map.value(QStringLiteral("id")).toString() == activeId) {
            if (!firstUserPrompt.isEmpty() && map.value(QStringLiteral("title")).toString() == QLatin1String("New Conversation")) {
                map[QStringLiteral("title")] = firstUserPrompt;
            }
            if (!lastAssistantReply.isEmpty()) {
                QString cleanPreview = lastAssistantReply;
                cleanPreview.remove(QRegularExpression(QStringLiteral("[#*`>]+")));
                map[QStringLiteral("preview")] = cleanPreview.simplified().left(180);
            }
            map[QStringLiteral("timeLabel")] = QDateTime::currentDateTime().toString(QStringLiteral("h:mm AP"));
            map[QStringLiteral("updatedAtMs")] = QDateTime::currentMSecsSinceEpoch();
            map[QStringLiteral("messages")] = messages;
            conversations[i] = map;
            saveConversations(conversations);
            break;
        }
    }
}

QString ConversationManager::createNewConversation(QVariantList &conversations)
{
    const QString newId = QStringLiteral("conv-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    QVariantMap map;
    map[QStringLiteral("id")] = newId;
    map[QStringLiteral("timeLabel")] = QDateTime::currentDateTime().toString(QStringLiteral("h:mm AP"));
    map[QStringLiteral("title")] = QStringLiteral("New Conversation");
    map[QStringLiteral("preview")] = QStringLiteral("Ask Kity anything or start speaking...");
    map[QStringLiteral("pinned")] = false;
    map[QStringLiteral("updatedAtMs")] = QDateTime::currentMSecsSinceEpoch();
    map[QStringLiteral("messages")] = QVariantList();
    conversations.prepend(map);
    saveConversations(conversations);
    return newId;
}

bool ConversationManager::selectConversation(const QVariantList &conversations, const QString &id, QVariantList &messagesOut)
{
    for (const QVariant &v : std::as_const(conversations)) {
        const QVariantMap map = v.toMap();
        if (map.value(QStringLiteral("id")).toString() == id) {
            messagesOut = map.value(QStringLiteral("messages")).toList();
            return true;
        }
    }
    return false;
}

void ConversationManager::togglePin(QVariantList &conversations, const QString &id)
{
    for (int i = 0; i < conversations.size(); ++i) {
        QVariantMap map = conversations[i].toMap();
        if (map.value(QStringLiteral("id")).toString() == id) {
            map[QStringLiteral("pinned")] = !map.value(QStringLiteral("pinned")).toBool();
            conversations[i] = map;
            saveConversations(conversations);
            return;
        }
    }
}

void ConversationManager::rename(QVariantList &conversations, const QString &id, const QString &newTitle)
{
    const QString trimmed = newTitle.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    for (int i = 0; i < conversations.size(); ++i) {
        QVariantMap map = conversations[i].toMap();
        if (map.value(QStringLiteral("id")).toString() == id) {
            map[QStringLiteral("title")] = trimmed;
            conversations[i] = map;
            saveConversations(conversations);
            return;
        }
    }
}

void ConversationManager::deleteConversations(QVariantList &conversations, const QStringList &ids, QString &activeIdInOut, QVariantList &activeMessagesOut)
{
    if (ids.isEmpty()) {
        return;
    }
    bool removedAny = false;
    for (int i = conversations.size() - 1; i >= 0; --i) {
        if (ids.contains(conversations[i].toMap().value(QStringLiteral("id")).toString())) {
            conversations.removeAt(i);
            removedAny = true;
        }
    }
    if (removedAny) {
        if (conversations.isEmpty()) {
            activeIdInOut = createNewConversation(conversations);
            activeMessagesOut.clear();
            return;
        }
        saveConversations(conversations);
        if (ids.contains(activeIdInOut)) {
            const QVariantMap firstMap = conversations.first().toMap();
            activeIdInOut = firstMap.value(QStringLiteral("id")).toString();
            activeMessagesOut = firstMap.value(QStringLiteral("messages")).toList();
        }
    }
}

QString ConversationManager::processAttachedFiles(const QStringList &fileUrls)
{
    if (fileUrls.isEmpty()) {
        return QString();
    }
    QStringList blocks;
    QStringList names;
    for (const QString &raw : fileUrls) {
        QString localPath = raw;
        if (localPath.startsWith(QLatin1String("file://"))) {
            localPath = QUrl(localPath).toLocalFile();
        }
        QFileInfo fi(localPath);
        if (!fi.exists() || !fi.isFile()) {
            continue;
        }
        names.append(fi.fileName());
        const QString ext = fi.suffix().toLower();
        QString extracted;
        if (ext == QLatin1String("pdf")) {
            QProcess pPdf;
            pPdf.start(QStringLiteral("pdftotext"), {localPath, QStringLiteral("-")});
            pPdf.waitForFinished(4000);
            extracted = QString::fromUtf8(pPdf.readAllStandardOutput()).trimmed();
        } else if (ext == QLatin1String("png") || ext == QLatin1String("jpg") || ext == QLatin1String("jpeg") || ext == QLatin1String("webp")) {
            QProcess pOcr;
            pOcr.start(QStringLiteral("tesseract"), {localPath, QStringLiteral("stdout"), QStringLiteral("--psm"), QStringLiteral("3")});
            pOcr.waitForFinished(4000);
            extracted = QString::fromUtf8(pOcr.readAllStandardOutput()).trimmed();
            if (extracted.isEmpty()) {
                extracted = QStringLiteral("[Imagen adjunta: %1]").arg(fi.fileName());
            }
        } else {
            QFile f(localPath);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                extracted = QString::fromUtf8(f.readAll()).trimmed();
            }
        }
        if (!extracted.isEmpty()) {
            blocks.append(QStringLiteral("### %1\n%2").arg(fi.fileName(), extracted.left(6000)));
        }
    }
    if (blocks.isEmpty()) {
        return QString();
    }
    return QStringLiteral("He adjuntado %1 (%2). Analiza su contenido y dime los puntos principales:\n\n%3")
        .arg(names.size() == 1 ? QStringLiteral("el archivo") : QStringLiteral("los archivos"),
             names.join(QStringLiteral(", ")),
             blocks.join(QStringLiteral("\n\n")));
}

} // namespace Kity
