/*
    SPDX-FileCopyrightText: 2026 Kiri contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kirigami as Kirigami

Rectangle {
    id: glassRoot

    property bool liquidGlass: false
    property bool isHighlighted: false
    // "shell" (outer container), "recessed" (inner well), or "card" (floating bubble/bar)
    property string glassStyle: "card"
    property real glassOpacity: 1.0

    radius: glassStyle === "shell" ? 0 : (glassStyle === "card" ? Kirigami.Units.smallSpacing : Kirigami.Units.mediumSpacing)

    // Respects KDE Plasma / Breeze theme natively (supports both light and dark themes)
    color: {
        if (glassStyle === "shell") {
            // For the outer shell on a default Plasma popup, stay transparent so Plasma draws the native background
            return isHighlighted
                ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.08)
                : "transparent";
        }
        if (isHighlighted) {
            return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.18);
        }
        if (glassStyle === "recessed") {
            return Kirigami.Theme.alternateBackgroundColor;
        }
        return Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.alternateBackgroundColor, 0.6);
    }

    border.width: glassStyle === "shell" ? 0 : 1
    border.color: {
        if (isHighlighted) {
            return Kirigami.Theme.highlightColor;
        }
        return Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.14);
    }
}

