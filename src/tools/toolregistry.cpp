/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "toolregistry.h"
#include "launchapptool.h"
#include "closeapptool.h"
#include "mediacontroltool.h"
#include "timertool.h"
#include "weathertool.h"
#include "systemvitalstool.h"
#include "clipboardtool.h"
#include "terminalcommandtool.h"
#include "websearchtool.h"
#include "calendareventtool.h"
#include "emailtool.h"
#include "notificationtool.h"

namespace Kity {

ToolRegistry &ToolRegistry::instance()
{
    static ToolRegistry s_instance;
    return s_instance;
}

ToolRegistry::ToolRegistry()
{
    registerDefaultTools();
}

void ToolRegistry::registerDefaultTools()
{
    registerTool(std::make_shared<LaunchAppTool>());
    registerTool(std::make_shared<CloseAppTool>());
    registerTool(std::make_shared<MediaControlTool>());
    registerTool(std::make_shared<TimerTool>());
    registerTool(std::make_shared<WeatherTool>());
    registerTool(std::make_shared<SystemVitalsTool>());
    registerTool(std::make_shared<ClipboardTool>());
    registerTool(std::make_shared<TerminalCommandTool>());
    registerTool(std::make_shared<WebSearchTool>());
    registerTool(std::make_shared<CalendarEventTool>());
    registerTool(std::make_shared<EmailTool>());
    registerTool(std::make_shared<NotificationTool>());
}

void ToolRegistry::registerTool(std::shared_ptr<ITool> tool)
{
    if (tool) {
        m_tools.insert(tool->name(), tool);
    }
}

std::shared_ptr<ITool> ToolRegistry::findTool(const QString &name) const
{
    return m_tools.value(name, nullptr);
}

QJsonArray ToolRegistry::getToolDefinitions(bool isLightModel, const QString &userQuery) const
{
    QJsonArray toolsArray;
    const QString q = userQuery.toLower().trimmed();

    for (auto it = m_tools.constBegin(); it != m_tools.constEnd(); ++it) {
        const auto &tool = it.value();
        if (isLightModel) {
            if (!tool->isLightModelCompatible()) {
                continue;
            }
            if (!q.isEmpty() && !tool->matchesQuery(q)) {
                continue;
            }
        }

        QJsonObject fn;
        fn[QStringLiteral("name")] = tool->name();
        fn[QStringLiteral("description")] = tool->description();
        fn[QStringLiteral("parameters")] = tool->parameters();

        QJsonObject toolObj;
        toolObj[QStringLiteral("type")] = QStringLiteral("function");
        toolObj[QStringLiteral("function")] = fn;

        toolsArray.append(toolObj);
    }

    return toolsArray;
}

QJsonObject ToolRegistry::executeTool(const QString &name, const QJsonObject &arguments)
{
    auto tool = findTool(name);
    if (!tool) {
        QJsonObject err;
        err[QStringLiteral("error")] = QStringLiteral("Tool '%1' not found").arg(name);
        return err;
    }

    return tool->execute(arguments);
}

} // namespace Kity
