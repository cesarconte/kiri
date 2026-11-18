/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "systemvitalstool.h"
#include "../system/hardwaremanager.h"

namespace Kity {

QJsonObject SystemVitalsTool::parameters() const
{
    QJsonObject params;
    params[QStringLiteral("type")] = QStringLiteral("object");
    params[QStringLiteral("properties")] = QJsonObject();
    return params;
}

QJsonObject SystemVitalsTool::execute(const QJsonObject &arguments)
{
    Q_UNUSED(arguments);
    QJsonObject result;
    result[QStringLiteral("success")] = true;
    result[QStringLiteral("vitals")] = HardwareManager::instance().collectSystemVitals();
    result[QStringLiteral("vitals_data")] = HardwareManager::instance().getVitalsJson();
    return result;
}

} // namespace Kity
