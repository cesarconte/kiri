/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <KRunner/AbstractRunner>
#include <KRunner/Action>
#include <KRunner/QueryMatch>
#include <KRunner/RunnerContext>

class KityRunner : public KRunner::AbstractRunner
{
    Q_OBJECT

public:
    explicit KityRunner(QObject *parent, const KPluginMetaData &metaData);
    ~KityRunner() override = default;

    void init() override;
    void match(KRunner::RunnerContext &context) override;
    void run(const KRunner::RunnerContext &context, const KRunner::QueryMatch &match) override;

private:
    struct KnowledgeHit {
        QString filePath;
        QString fileName;
        QString preview;
        double relevance = 0.0;
    };

    QList<KnowledgeHit> searchKnowledgeBase(const QString &query) const;
};
