/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "clipboardtool.h"
#include "../system/clipboardmanager.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace Kity {

bool ClipboardTool::matchesQuery(const QString &query) const
{
    static const QRegularExpression clipSignal(
        QStringLiteral("(portapapeles|clipboard|copiado|copi[eé]|klipper|historial)"),
        QRegularExpression::CaseInsensitiveOption);
    return query.contains(clipSignal);
}

QJsonObject ClipboardTool::parameters() const
{
    QJsonObject props;

    QJsonObject actProp;
    actProp[QStringLiteral("type")] = QStringLiteral("string");
    QJsonArray actEnum;
    actEnum.append(QStringLiteral("get_current"));
    actEnum.append(QStringLiteral("list_history"));
    actEnum.append(QStringLiteral("search"));
    actProp[QStringLiteral("enum")] = actEnum;
    actProp[QStringLiteral("description")] = QStringLiteral("Acción: 'get_current' (obtener lo último copiado), 'list_history' (ver historial reciente) o 'search' (buscar texto copiado).");
    props[QStringLiteral("action")] = actProp;

    QJsonObject qProp;
    qProp[QStringLiteral("type")] = QStringLiteral("string");
    qProp[QStringLiteral("description")] = QStringLiteral("Texto o término a buscar en el historial del portapapeles cuando action='search'.");
    props[QStringLiteral("query")] = qProp;

    QJsonObject params;
    params[QStringLiteral("type")] = QStringLiteral("object");
    params[QStringLiteral("properties")] = props;
    QJsonArray req;
    req.append(QStringLiteral("action"));
    params[QStringLiteral("required")] = req;

    return params;
}

QJsonObject ClipboardTool::execute(const QJsonObject &arguments)
{
    const QString action = arguments.value(QStringLiteral("action")).toString().trimmed().toLower();
    QJsonObject result;
    result[QStringLiteral("action")] = action;

    if (action == QLatin1String("list_history")) {
        const QStringList history = ClipboardManager::instance().getClipboardHistory(10);
        result[QStringLiteral("history")] = history.join(QStringLiteral("\n---\n"));
        result[QStringLiteral("count")] = history.size();
        result[QStringLiteral("success")] = true;
        return result;
    }

    if (action == QLatin1String("search")) {
        const QString query = arguments.value(QStringLiteral("query")).toString().trimmed();
        const QStringList matches = ClipboardManager::instance().searchClipboardHistory(query, 5);
        result[QStringLiteral("query")] = query;
        result[QStringLiteral("matches")] = matches.isEmpty()
            ? QStringLiteral("No se encontraron coincidencias en el historial del portapapeles.")
            : matches.join(QStringLiteral("\n---\n"));
        result[QStringLiteral("success")] = true;
        return result;
    }

    // Default / "get_current"
    const QString current = ClipboardManager::instance().getCurrentClipboard();
    result[QStringLiteral("clipboard_content")] = current.isEmpty() ? QStringLiteral("(El portapapeles está vacío)") : current;
    result[QStringLiteral("success")] = true;
    return result;
}

} // namespace Kity
