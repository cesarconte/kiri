/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace Kity {

class KnowledgeManager : public QObject
{
    Q_OBJECT

public:
    static KnowledgeManager &instance();

    void ensureKnowledgeBaseReady();
    QString fetchKnowledgeBaseContext(const QString &query, int maxSnippets = 3) const;
    void openKnowledgeFolder() const;
    int knowledgeDocumentCount() const;
    QString knowledgeDirectoryPath() const;

private:
    explicit KnowledgeManager(QObject *parent = nullptr);
    ~KnowledgeManager() override = default;

    KnowledgeManager(const KnowledgeManager &) = delete;
    KnowledgeManager &operator=(const KnowledgeManager &) = delete;
};

} // namespace Kity
