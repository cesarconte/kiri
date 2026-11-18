/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QString>
#include <QStringList>

namespace Kity {

/**
 * @brief ClipboardManager encapsulates reading and writing to Wayland selection / clipboard
 * as well as querying KDE Plasma's Klipper clipboard manager via D-Bus.
 */
class ClipboardManager
{
public:
    static ClipboardManager &instance();

    // Direct Wayland selection & clipboard access
    QString readWaylandSelectionOrClipboard() const;
    void copyToClipboard(const QString &text) const;

    // Klipper KDE Clipboard Manager integration
    QString getCurrentClipboard() const;
    QStringList getClipboardHistory(int limit = 10) const;
    QStringList searchClipboardHistory(const QString &query, int limit = 5) const;

private:
    ClipboardManager() = default;
    ~ClipboardManager() = default;
    ClipboardManager(const ClipboardManager &) = delete;
    ClipboardManager &operator=(const ClipboardManager &) = delete;
};

} // namespace Kity
