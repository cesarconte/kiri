/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "knowledgemanager.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>

namespace Kity {

KnowledgeManager &KnowledgeManager::instance()
{
    static KnowledgeManager s_instance;
    return s_instance;
}

KnowledgeManager::KnowledgeManager(QObject *parent)
    : QObject(parent)
{
    ensureKnowledgeBaseReady();
}

QString KnowledgeManager::knowledgeDirectoryPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity/knowledge");
}

void KnowledgeManager::ensureKnowledgeBaseReady()
{
    const QString kbPath = knowledgeDirectoryPath();
    QDir dir(kbPath);
    if (!dir.exists()) {
        dir.mkpath(kbPath);
    }
}

void KnowledgeManager::openKnowledgeFolder() const
{
    const QString kbPath = knowledgeDirectoryPath();
    QDir().mkpath(kbPath);
    QDesktopServices::openUrl(QUrl::fromLocalFile(kbPath));
}

int KnowledgeManager::knowledgeDocumentCount() const
{
    const QString kbPath = knowledgeDirectoryPath();
    const QDir dir(kbPath);
    if (!dir.exists()) {
        return 0;
    }
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.txt") << QStringLiteral("*.md") << QStringLiteral("*.pdf"), QDir::Files);
    int count = 0;
    for (const auto &f : files) {
        if (!f.startsWith(QStringLiteral("README"), Qt::CaseInsensitive)) {
            count++;
        }
    }
    return count;
}

QString KnowledgeManager::fetchKnowledgeBaseContext(const QString &query, int maxSnippets) const
{
    const QString kbDir = knowledgeDirectoryPath();
    QDir dir(kbDir);
    if (!dir.exists()) {
        return QString();
    }

    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.txt") << QStringLiteral("*.md") << QStringLiteral("*.pdf"), QDir::Files);
    if (files.isEmpty()) {
        return QString();
    }

    static const QSet<QString> stopWords = {
        QStringLiteral("el"), QStringLiteral("la"), QStringLiteral("los"), QStringLiteral("las"),
        QStringLiteral("un"), QStringLiteral("una"), QStringLiteral("unos"), QStringLiteral("unas"),
        QStringLiteral("de"), QStringLiteral("del"), QStringLiteral("en"), QStringLiteral("por"),
        QStringLiteral("para"), QStringLiteral("con"), QStringLiteral("sin"), QStringLiteral("sobre"),
        QStringLiteral("que"), QStringLiteral("qué"), QStringLiteral("como"), QStringLiteral("cómo"),
        QStringLiteral("cual"), QStringLiteral("cuál"), QStringLiteral("donde"), QStringLiteral("dónde"),
        QStringLiteral("cuando"), QStringLiteral("cuándo"), QStringLiteral("quien"), QStringLiteral("quién"),
        QStringLiteral("dime"), QStringLiteral("busca"), QStringLiteral("saber"), QStringLiteral("tengo"),
        QStringLiteral("este"), QStringLiteral("esta"), QStringLiteral("estos"), QStringLiteral("estas"),
        QStringLiteral("the"), QStringLiteral("and"), QStringLiteral("for"), QStringLiteral("with")
    };

    const QStringList tokens = query.toLower().split(QRegularExpression(QStringLiteral("[\\s,;:.!?¡¿]+")), Qt::SkipEmptyParts);
    QStringList keywords;
    for (const QString &tok : tokens) {
        if (tok.length() >= 3 && !stopWords.contains(tok)) {
            keywords.append(tok);
        }
    }

    if (keywords.isEmpty()) {
        return QString();
    }

    struct MatchItem {
        QString filename;
        QString snippet;
        int score = 0;
    };

    QList<MatchItem> matches;

    for (const QString &fname : files) {
        if (fname.startsWith(QStringLiteral("README"), Qt::CaseInsensitive)) {
            continue;
        }

        QString content;
        if (fname.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
            const QString cachedTxtPath = dir.filePath(fname + QStringLiteral(".txt"));
            if (!QFileInfo::exists(cachedTxtPath)) {
                QProcess pPdf;
                pPdf.start(QStringLiteral("pdftotext"), {dir.filePath(fname), cachedTxtPath});
                pPdf.waitForFinished(3000);
            }
            QFile fTxt(cachedTxtPath);
            if (fTxt.open(QIODevice::ReadOnly | QIODevice::Text)) {
                content = QString::fromUtf8(fTxt.readAll());
            }
        } else {
            QFile f(dir.filePath(fname));
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                content = QString::fromUtf8(f.readAll());
            }
        }

        if (!content.isEmpty()) {
            int score = 0;
            for (const QString &kw : keywords) {
                if (content.contains(kw, Qt::CaseInsensitive)) {
                    score += 2;
                }
                if (fname.contains(kw, Qt::CaseInsensitive)) {
                    score += 3;
                }
            }

            if (score > 0) {
                MatchItem item;
                item.filename = fname;
                item.score = score;
                item.snippet = content.left(1200);
                matches.append(item);
            }
        }
    }

    if (matches.isEmpty()) {
        return QString();
    }

    std::sort(matches.begin(), matches.end(), [](const MatchItem &a, const MatchItem &b) {
        return a.score > b.score;
    });

    QString accumulated;
    accumulated.append(QStringLiteral("[Local Knowledge Documents (RAG):]\n"));
    const int count = qMin(matches.size(), maxSnippets <= 0 ? 3 : maxSnippets);
    for (int i = 0; i < count; ++i) {
        accumulated.append(QStringLiteral("--- File: %1 ---\n%2\n\n").arg(matches.at(i).filename, matches.at(i).snippet.trimmed()));
    }

    return accumulated.trimmed();
}

} // namespace Kity
