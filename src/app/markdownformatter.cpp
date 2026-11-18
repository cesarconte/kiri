/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "markdownformatter.h"

#include <QRegularExpression>
#include <QTextDocument>

namespace Kity {

QString MarkdownFormatter::renderMarkdown(const QString &markdown)
{
    QString normalized = markdown;
    // Prevent indented bullets ("    • " or "    - ") from being parsed as non-wrapping <pre> blocks
    static const QRegularExpression indentedBulletRe(
        QStringLiteral("(?m)^[ \\t]{2,}([•\\-\\*]\\s+)"));
    normalized.replace(indentedBulletRe, QStringLiteral("- "));

    QTextDocument doc;
    doc.setMarkdown(normalized);
    QString html = doc.toHtml();
    // Ensure <pre> blocks wrap instead of overflowing horizontally
    html.replace(QStringLiteral("white-space: pre;"), QStringLiteral("white-space: pre-wrap; word-wrap: break-word;"));
    html.replace(QStringLiteral("white-space:pre;"), QStringLiteral("white-space: pre-wrap; word-wrap: break-word;"));
    return html;
}

QStringList MarkdownFormatter::extractTerminalCommands(const QString &markdown)
{
    QStringList commands;
    static const QRegularExpression codeRegex(QStringLiteral("```(?:bash|sh|zsh)?\\s*\\n([\\s\\S]*?)\\n```"));
    auto it = codeRegex.globalMatch(markdown);
    while (it.hasNext()) {
        auto m = it.next();
        commands.append(m.captured(1).trimmed());
    }
    return commands;
}

} // namespace Kity
