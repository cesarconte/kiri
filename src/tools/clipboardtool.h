/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "itool.h"

namespace Kity {

class ClipboardTool : public ITool
{
public:
    ClipboardTool() = default;
    ~ClipboardTool() override = default;

    QString name() const override { return QStringLiteral("clipboard_tool"); }
    QString description() const override {
        return QStringLiteral("Accede al gestor del portapapeles de KDE Plasma (Klipper): lee el contenido actual copiado, consulta el historial reciente de copias o busca fragmentos de texto copiados con anterioridad.");
    }
    QJsonObject parameters() const override;
    QJsonObject execute(const QJsonObject &arguments) override;
};

} // namespace Kity
