/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QJsonObject>
#include <QString>

namespace Kity {

class ITool
{
public:
    virtual ~ITool() = default;

    virtual QString name() const = 0;
    virtual QString description() const = 0;
    virtual QJsonObject parameters() const = 0;
    virtual bool isLightModelCompatible() const { return true; }

    virtual QJsonObject execute(const QJsonObject &arguments) = 0;
};

} // namespace Kity
