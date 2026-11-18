/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace Kity {

struct IntentExecutionResult {
    bool handled = false;
    QString feedback;
    QString cardType;
    QVariantMap cardData;
};

/**
 * @brief IntentParser provides fast, zero-latency direct intent detection
 * for local system actions, delegating immediately to the specialized
 * system managers without requiring an LLM round-trip.
 */
class IntentParser : public QObject
{
    Q_OBJECT

public:
    static IntentParser &instance();

    IntentExecutionResult execute(const QString &input);

private:
    explicit IntentParser(QObject *parent = nullptr);
    ~IntentParser() override = default;

    IntentParser(const IntentParser &) = delete;
    IntentParser &operator=(const IntentParser &) = delete;
};

} // namespace Kity
