/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "websearchtool.h"
#include "../system/websearchmanager.h"

#include <QJsonArray>

namespace Kity {

QJsonObject WebSearchTool::parameters() const
{
    QJsonObject props;

    QJsonObject queryProp;
    queryProp[QStringLiteral("type")] = QStringLiteral("string");
    queryProp[QStringLiteral("description")] = QStringLiteral("Términos o consulta de búsqueda en internet.");
    props[QStringLiteral("query")] = queryProp;

    QJsonObject params;
    params[QStringLiteral("type")] = QStringLiteral("object");
    params[QStringLiteral("properties")] = props;
    QJsonArray req;
    req.append(QStringLiteral("query"));
    params[QStringLiteral("required")] = req;

    return params;
}

QJsonObject WebSearchTool::execute(const QJsonObject &arguments)
{
    const QString query = arguments.value(QStringLiteral("query")).toString().trimmed();
    const QString searchRes = WebSearchManager::instance().fetchWebSearchContext(query);

    QJsonObject result;
    result[QStringLiteral("success")] = !searchRes.isEmpty();
    result[QStringLiteral("query")] = query;
    result[QStringLiteral("results")] = searchRes.isEmpty()
        ? QStringLiteral("No se encontraron resultados en la web.")
        : searchRes;
    return result;
}

} // namespace Kity
