/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "clipboardmanager.h"

#include <QClipboard>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QGuiApplication>
#include <QProcess>

namespace Kity {

ClipboardManager &ClipboardManager::instance()
{
    static ClipboardManager s_instance;
    return s_instance;
}

QString ClipboardManager::readWaylandSelectionOrClipboard() const
{
    // 1. Try primary Wayland selection first via wl-paste (works globally across unfocused clients)
    QProcess process;
    process.start(QStringLiteral("wl-paste"), {QStringLiteral("-p"), QStringLiteral("--no-newline")});
    if (process.waitForFinished(800)) {
        const QString text = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        if (!text.isEmpty()) {
            return text;
        }
    }

    // 2. Try standard Wayland clipboard
    process.start(QStringLiteral("wl-paste"), {QStringLiteral("--no-newline")});
    if (process.waitForFinished(800)) {
        const QString text = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        if (!text.isEmpty()) {
            return text;
        }
    }

    // 3. Fallback to Qt clipboard
    QClipboard *cb = QGuiApplication::clipboard();
    if (!cb) {
        return QString();
    }
    QString text = cb->text(QClipboard::Selection).trimmed();
    if (text.isEmpty()) {
        text = cb->text(QClipboard::Clipboard).trimmed();
    }
    return text;
}

void ClipboardManager::copyToClipboard(const QString &text) const
{
    if (auto clip = QGuiApplication::clipboard()) {
        clip->setText(text, QClipboard::Clipboard);
        clip->setText(text, QClipboard::Selection);
    }
    // Also use wl-copy to ensure Wayland clipboard and primary selection ownership
    // Detach with null std handles so background wl-copy daemons don't inherit caller/ctest pipes
    auto spawnWlCopy = [](const QStringList &args) {
        QProcess proc;
        proc.setProgram(QStringLiteral("wl-copy"));
        proc.setArguments(args);
        proc.setStandardOutputFile(QProcess::nullDevice());
        proc.setStandardErrorFile(QProcess::nullDevice());
        proc.setStandardInputFile(QProcess::nullDevice());
        proc.startDetached();
    };

    spawnWlCopy({text});
    spawnWlCopy({QStringLiteral("-p"), text});
}

QString ClipboardManager::getCurrentClipboard() const
{
    QDBusInterface klipperIface(QStringLiteral("org.kde.klipper"),
                                QStringLiteral("/klipper"),
                                QStringLiteral("org.kde.klipper.klipper"),
                                QDBusConnection::sessionBus());
    if (klipperIface.isValid()) {
        QDBusReply<QString> reply = klipperIface.call(QStringLiteral("getClipboardContents"));
        if (reply.isValid() && !reply.value().trimmed().isEmpty()) {
            return reply.value().trimmed();
        }
    }

    return readWaylandSelectionOrClipboard();
}

QStringList ClipboardManager::getClipboardHistory(int limit) const
{
    QStringList history;
    QDBusInterface klipperIface(QStringLiteral("org.kde.klipper"),
                                QStringLiteral("/klipper"),
                                QStringLiteral("org.kde.klipper.klipper"),
                                QDBusConnection::sessionBus());
    if (klipperIface.isValid()) {
        QDBusReply<QStringList> menuReply = klipperIface.call(QStringLiteral("getClipboardHistoryMenu"));
        if (menuReply.isValid() && !menuReply.value().isEmpty()) {
            history = menuReply.value();
        }
    }

    if (history.isEmpty()) {
        const QString current = readWaylandSelectionOrClipboard();
        if (!current.isEmpty()) {
            history.append(current);
        }
    }

    if (limit > 0 && history.size() > limit) {
        return history.mid(0, limit);
    }
    return history;
}

QStringList ClipboardManager::searchClipboardHistory(const QString &query, int limit) const
{
    const QString trimmedQuery = query.trimmed();
    if (trimmedQuery.isEmpty()) {
        return getClipboardHistory(limit);
    }

    QStringList matches;
    const QStringList history = getClipboardHistory(0);
    for (const QString &item : history) {
        if (item.contains(trimmedQuery, Qt::CaseInsensitive)) {
            matches.append(item);
            if (limit > 0 && matches.size() >= limit) {
                break;
            }
        }
    }
    return matches;
}

} // namespace Kity
