/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace Kity {

struct VisualResult {
    bool success = false;
    bool handledEarly = false;
    QString errorMessage;
    QString assistantFeedback;
    QString userMessageLabel;
    QString fullPrompt;
    QString base64Image;
    QString modeLabel;
};

class VisualIntelligenceController : public QObject
{
    Q_OBJECT

public:
    static VisualIntelligenceController &instance();

    VisualResult process(const QString &captureMode, const QString &action, const QString &customQuestion);

private:
    explicit VisualIntelligenceController(QObject *parent = nullptr);
    ~VisualIntelligenceController() override = default;

    VisualIntelligenceController(const VisualIntelligenceController &) = delete;
    VisualIntelligenceController &operator=(const VisualIntelligenceController &) = delete;
};

} // namespace Kity
