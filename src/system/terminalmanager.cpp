/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "terminalmanager.h"

#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace Kity {

TerminalManager &TerminalManager::instance()
{
    static TerminalManager s_instance;
    return s_instance;
}

QString TerminalManager::sanitizeCommand(const QString &rawCommand) const
{
    QString cmd = rawCommand.trimmed();
    const QString defaultUpdater = !QStandardPaths::findExecutable(QStringLiteral("cachy-update")).isEmpty()
        ? QStringLiteral("cachy-update")
        : QStringLiteral("sudo pacman -Syu");

    if (cmd.contains(QStringLiteral("apt update")) || cmd.contains(QStringLiteral("apt-get update"))
        || cmd.contains(QStringLiteral("apt upgrade")) || cmd.contains(QStringLiteral("apt-get upgrade"))
        || cmd.contains(QStringLiteral("apt install")) || cmd.contains(QStringLiteral("apt-get install"))) {
        cmd = defaultUpdater;
    }
    if (cmd.isEmpty()) {
        cmd = defaultUpdater;
    }
    return cmd;
}

bool TerminalManager::isUpdateCommand(const QString &command) const
{
    return command.contains(QStringLiteral("cachy-update"), Qt::CaseInsensitive)
        || command.contains(QStringLiteral("arch-update"), Qt::CaseInsensitive)
        || command.contains(QStringLiteral("pacman -S"), Qt::CaseInsensitive)
        || command.contains(QStringLiteral("pacman -Syu"), Qt::CaseInsensitive)
        || command.contains(QStringLiteral("pacman -Syyu"), Qt::CaseInsensitive)
        || command.contains(QStringLiteral("paru"), Qt::CaseInsensitive)
        || command.contains(QStringLiteral("yay"), Qt::CaseInsensitive)
        || command.contains(QStringLiteral("flatpak update"), Qt::CaseInsensitive);
}

QString TerminalManager::wrapUpdateCommand(const QString &command) const
{
    return QStringLiteral(
        "%1; RET=$?; "
        "if [ $RET -eq 0 ]; then "
            "echo -e \"\\n\\033[1;32m✓ Proceso finalizado con éxito.\\033[0m Cerrando terminal en 4 segundos (pulsa cualquier tecla para cerrar de inmediato)...\"; "
            "read -t 4 -n 1 -s -r; "
            "exit 0; "
        "else "
            "echo -e \"\\n\\033[1;31m✗ El proceso no se completó o fue cancelado (código $RET).\\033[0m\"; "
            "read -n 1 -s -r -p \"Presiona cualquier tecla para cerrar la ventana...\"; "
            "exit $RET; "
        "fi"
    ).arg(command);
}

bool TerminalManager::executeInKonsole(const QString &command,
                                      const QString &customTitle,
                                      bool forceAutoClose) const
{
    const QString konsole = QStandardPaths::findExecutable(QStringLiteral("konsole"));
    if (konsole.isEmpty()) {
        return false;
    }

    const QString cmd = sanitizeCommand(command);
    const bool isUpdate = isUpdateCommand(cmd) || forceAutoClose;

    QStringList args;
    if (isUpdate) {
        if (!customTitle.isEmpty()) {
            args << QStringLiteral("-p") << QStringLiteral("tabtitle=%1").arg(customTitle);
        } else if (cmd.contains(QStringLiteral("cachy-update"), Qt::CaseInsensitive)) {
            args << QStringLiteral("-p") << QStringLiteral("tabtitle=Cachy-Update");
        } else {
            args << QStringLiteral("-p") << QStringLiteral("tabtitle=Actualización del Sistema");
        }
        args << QStringLiteral("-e") << QStringLiteral("bash") << QStringLiteral("-c") << wrapUpdateCommand(cmd);
    } else {
        args << QStringLiteral("--noclose");
        if (!customTitle.isEmpty()) {
            args << QStringLiteral("-p") << QStringLiteral("tabtitle=%1").arg(customTitle);
        }
        args << QStringLiteral("-e") << QStringLiteral("bash") << QStringLiteral("-c") << cmd;
    }

    return QProcess::startDetached(konsole, args);
}

QStringList TerminalManager::extractTerminalCommands(const QString &markdown) const
{
    QStringList commands;
    static const QRegularExpression codeRegex(QStringLiteral("```(?:bash|sh|zsh)?\\s*\\n([\\s\\S]*?)\\n```"));
    auto it = codeRegex.globalMatch(markdown);
    while (it.hasNext()) {
        auto m = it.next();
        commands.append(m.captured(1).trimmed());
    }
    return commands;
}

} // namespace Kity
