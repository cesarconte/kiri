/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QString>
#include <QStringList>

/**
 * macOS "Math Notes": a scratchpad where every line is evaluated live and variables
 * persist across lines (`precio = 12.5`, `total = precio * 8`). Evaluation is delegated
 * to libqalculate's `qalc` (units, currencies, percentages) in a locale-neutral session.
 */
class MathNotes
{
public:
    /// One result string per input line ("" for blank lines, comments and plain prose).
    static QStringList evaluateLines(const QString &text, int timeoutMs = 2500);

    // Pure helpers (unit-testable without qalc)
    static bool isEvaluableLine(const QString &line);
    static QString normalizeExpression(const QString &line);
    static QStringList parseQalcOutput(const QString &output, const QStringList &sentLines);
    static QString stripAnsi(const QString &s);
};
