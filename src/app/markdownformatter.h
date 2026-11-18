/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QString>
#include <QStringList>

namespace Kity {

class MarkdownFormatter
{
public:
    static QString renderMarkdown(const QString &markdown);
    static QStringList extractTerminalCommands(const QString &markdown);
};

} // namespace Kity
