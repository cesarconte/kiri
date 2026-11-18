/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "writingtoolsmanager.h"
#include "../mathnotes.h"

#include <QHash>
#include <QRegularExpression>

namespace Kity {

WritingToolsManager &WritingToolsManager::instance()
{
    static WritingToolsManager s_instance;
    return s_instance;
}

WritingToolsManager::WritingToolsManager(QObject *parent)
    : QObject(parent)
{
}

QString WritingToolsManager::languageDisplayName(const QString &code) const
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("es"), QStringLiteral("español de España")}, {QStringLiteral("en"), QStringLiteral("inglés")},
        {QStringLiteral("fr"), QStringLiteral("francés")},           {QStringLiteral("de"), QStringLiteral("alemán")},
        {QStringLiteral("it"), QStringLiteral("italiano")},          {QStringLiteral("pt"), QStringLiteral("portugués")},
        {QStringLiteral("ca"), QStringLiteral("catalán")},           {QStringLiteral("ja"), QStringLiteral("japonés")},
        {QStringLiteral("zh"), QStringLiteral("chino simplificado")},
    };
    const QString c = code.trimmed().toLower();
    return names.value(c, c.isEmpty() ? QStringLiteral("español de España") : c);
}

QString WritingToolsManager::buildPrompt(const QString &action, const QString &sourceText) const
{
    const QString cleanText = sourceText.trimmed();
    if (cleanText.isEmpty()) {
        return QString();
    }

    QString instruction;
    if (action.startsWith(QStringLiteral("translate:"))) {
        const QString target = languageDisplayName(action.mid(10));
        instruction = QStringLiteral(
            "Eres un traductor experto. Traduce el siguiente texto a %1 de forma completamente natural y fiel, "
            "detectando automáticamente el idioma de origen. Devuelve únicamente la traducción, sin notas ni comentarios.").arg(target);
    } else if (action == QStringLiteral("proofread")) {
        instruction = QStringLiteral(
            "Eres un corrector de estilo y ortografía profesional. "
            "Corrige la ortografía, gramática y puntuación del siguiente texto en español de España, manteniendo su significado y estilo original. "
            "Devuelve directamente el texto corregido listo para copiar y, debajo, una breve nota de 1 o 2 líneas resumiendo las mejoras aplicadas.");
    } else if (action == QStringLiteral("rewrite")) {
        instruction = QStringLiteral(
            "Eres un redactor profesional. "
            "Reescribe el siguiente texto mejorando su fluidez, claridad, vocabulario y elegancia en español de España. "
            "Devuelve únicamente la versión reescrita lista para copiar, sin preámbulos.");
    } else if (action == QStringLiteral("friendly")) {
        instruction = QStringLiteral(
            "Eres un redactor experto en comunicación cercana. "
            "Reescribe el siguiente texto con un tono cercano, cálido y amigable (Friendly tone) en español de España. "
            "Devuelve únicamente el texto resultante listo para usar.");
    } else if (action == QStringLiteral("professional")) {
        instruction = QStringLiteral(
            "Eres un redactor corporativo de alto nivel. "
            "Reescribe el siguiente texto con un tono formal, ejecutivo y profesional (Professional tone) en español de España. "
            "Devuelve únicamente el texto resultante listo para usar.");
    } else if (action == QStringLiteral("concise")) {
        instruction = QStringLiteral(
            "Eres un editor de síntesis. "
            "Reescribe el siguiente texto de forma concisa, directa y breve (Concise tone), eliminando cualquier redundancia. "
            "Devuelve únicamente el texto resultante listo para usar.");
    } else if (action == QStringLiteral("summary")) {
        instruction = QStringLiteral(
            "Eres un especialista en síntesis de información. "
            "Genera un resumen ejecutivo claro y bien estructurado del siguiente texto en español de España.");
    } else if (action == QStringLiteral("keypoints")) {
        instruction = QStringLiteral(
            "Eres un analista de información. "
            "Extrae los puntos clave (Key Points) del siguiente texto en una lista con viñetas clara, estructurada y directa.");
    } else if (action == QStringLiteral("table")) {
        instruction = QStringLiteral(
            "Eres un organizador de información estructurada. "
            "Organiza y estructura todos los datos e información del siguiente texto en una tabla Markdown clara y elegante.");
    } else {
        instruction = QStringLiteral("Mejora y procesa el siguiente texto en español de España:");
    }

    return QStringLiteral("%1\n\nTexto original:\n\"\"\"\n%2\n\"\"\"").arg(instruction, cleanText.left(6000));
}

QStringList WritingToolsManager::evaluateMath(const QString &text) const
{
    return MathNotes::evaluateLines(text);
}

QString WritingToolsManager::renderMarkdown(const QString &markdown) const
{
    if (markdown.isEmpty()) {
        return QString();
    }

    QString working = markdown;
    QStringList codeBlocks;
    static const QRegularExpression fencedCodeRegex(QStringLiteral("```(?:[a-zA-Z0-9_+\\-]*)?\\s*\\n([\\s\\S]*?)```"));
    auto matchIterator = fencedCodeRegex.globalMatch(working);
    int placeholderIndex = 0;
    while (matchIterator.hasNext()) {
        const auto match = matchIterator.next();
        codeBlocks.append(match.captured(1).trimmed());
        const QString placeholder = QStringLiteral("@@CODEBLOCK_%1@@").arg(placeholderIndex++);
        working.replace(match.captured(0), placeholder);
    }

    // 2. Bold: **text** -> <b>text</b>
    static const QRegularExpression boldRegex(QStringLiteral("\\*\\*(.*?)\\*\\*"));
    working.replace(boldRegex, QStringLiteral("<b>\\1</b>"));

    // 3. Italic: *text* -> <i>text</i>
    static const QRegularExpression italicRegex(QStringLiteral("(?<!\\*)\\*(?!\\*)(.*?)(?<!\\*)\\*(?!\\*)"));
    working.replace(italicRegex, QStringLiteral("<i>\\1</i>"));

    // 4. Headers: # Heading -> <font size="+2"><b>Heading</b></font>
    static const QRegularExpression h1Regex(QStringLiteral("^#\\s+(.*?)$"), QRegularExpression::MultilineOption);
    working.replace(h1Regex, QStringLiteral("<br><font size=\"+2\"><b>\\1</b></font><br>"));

    static const QRegularExpression h2Regex(QStringLiteral("^##\\s+(.*?)$"), QRegularExpression::MultilineOption);
    working.replace(h2Regex, QStringLiteral("<br><font size=\"+1\"><b>\\1</b></font><br>"));

    static const QRegularExpression h3Regex(QStringLiteral("^###\\s+(.*?)$"), QRegularExpression::MultilineOption);
    working.replace(h3Regex, QStringLiteral("<br><b>\\1</b><br>"));

    // 5. Bullet points: - item or * item -> • item
    static const QRegularExpression bulletRegex(QStringLiteral("^[\\-\\*]\\s+(.*?)$"), QRegularExpression::MultilineOption);
    working.replace(bulletRegex, QStringLiteral("• \\1"));

    // 6. Numbered lists: 1. item
    static const QRegularExpression numListRegex(QStringLiteral("^(\\d+)\\.\\s+(.*?)$"), QRegularExpression::MultilineOption);
    working.replace(numListRegex, QStringLiteral("\\1. \\2"));

    // 7. Inline code: `code`
    static const QRegularExpression inlineCodeRegex(QStringLiteral("`([^`]+)`"));
    working.replace(inlineCodeRegex, QStringLiteral("<font color=\"#60A5FA\"><code>\\1</code></font>"));

    // 8. Re-insert formatted fenced code blocks
    for (int i = 0; i < codeBlocks.size(); ++i) {
        const QString placeholder = QStringLiteral("@@CODEBLOCK_%1@@").arg(i);
        const QString escaped = codeBlocks.at(i).toHtmlEscaped();
        const QString blockHtml = QStringLiteral(
            "<br><table width=\"100%\" bgcolor=\"#1e1e2e\" cellpadding=\"8\" style=\"border-radius:6px;\">"
            "<tr><td><pre style=\"margin:0;color:#cdd6f4;font-family:monospace;font-size:12px;\">"
            "%1"
            "</pre></td></tr></table><br>").arg(escaped);
        working.replace(placeholder, blockHtml);
    }

    // 9. Links: [text](url) -> <a href="url">text</a>
    static const QRegularExpression linkRegex(QStringLiteral("\\[([^\\]]+)\\]\\(([^\\)]+)\\)"));
    working.replace(linkRegex, QStringLiteral("<a href=\"\\2\">\\1</a>"));

    // 10. Line breaks -> <br>
    working.replace(QStringLiteral("\n\n"), QStringLiteral("<br><br>"));
    working.replace(QStringLiteral("\n"), QStringLiteral("<br>"));

    return working.trimmed();
}

} // namespace Kity
