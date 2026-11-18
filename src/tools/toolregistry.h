/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "itool.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <memory>

namespace Kity {

class ToolRegistry
{
public:
    static ToolRegistry &instance();

    void registerTool(std::shared_ptr<ITool> tool);
    std::shared_ptr<ITool> findTool(const QString &name) const;

    QJsonArray getToolDefinitions(bool isLightModel = false, const QString &userQuery = QString()) const;
    QJsonObject executeTool(const QString &name, const QJsonObject &arguments);

private:
    ToolRegistry();
    ~ToolRegistry() = default;

    ToolRegistry(const ToolRegistry &) = delete;
    ToolRegistry &operator=(const ToolRegistry &) = delete;

    void registerDefaultTools();

    QMap<QString, std::shared_ptr<ITool>> m_tools;
};

} // namespace Kity
