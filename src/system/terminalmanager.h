/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QString>
#include <QStringList>

namespace Kity {

/**
 * @brief TerminalManager encapsulates running shell commands in KDE Konsole,
 * extracting code blocks from LLM markdown, and wrapping CachyOS / Arch Linux
 * package updates (pacman, paru, yay, flatpak) with automatic status feedback.
 */
class TerminalManager
{
public:
    static TerminalManager &instance();

    // Sanitization & Classification
    QString sanitizeCommand(const QString &rawCommand) const;
    bool isUpdateCommand(const QString &command) const;
    QString wrapUpdateCommand(const QString &command) const;

    // Execution
    bool executeInKonsole(const QString &command,
                          const QString &customTitle = QString(),
                          bool forceAutoClose = false) const;

    // Markdown Helper
    QStringList extractTerminalCommands(const QString &markdown) const;

private:
    TerminalManager() = default;
    ~TerminalManager() = default;
    TerminalManager(const TerminalManager &) = delete;
    TerminalManager &operator=(const TerminalManager &) = delete;
};

} // namespace Kity
