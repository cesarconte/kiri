/*
    SPDX-FileCopyrightText: 2026 Kiri contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kirigami as Kirigami

Rectangle {
    id: glassRoot

    property bool liquidGlass: true
    property bool isHighlighted: false
    property real glassOpacity: 0.72
    property color tintColor: isHighlighted
        ? Kirigami.Theme.highlightColor
        : Kirigami.Theme.alternateBackgroundColor

    radius: Kirigami.Units.mediumSpacing

    // Base adaptive fill
    color: {
        if (!liquidGlass) {
            return isHighlighted
                ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.22)
                : Kirigami.Theme.alternateBackgroundColor;
        }
        return isHighlighted
            ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.20)
            : Qt.rgba(Kirigami.Theme.alternateBackgroundColor.r,
                      Kirigami.Theme.alternateBackgroundColor.g,
                      Kirigami.Theme.alternateBackgroundColor.b,
                      glassOpacity);
    }

    // Outer translucent hairline border
    border.width: 1
    border.color: {
        if (isHighlighted) {
            return Qt.rgba(Kirigami.Theme.highlightColor.r,
                           Kirigami.Theme.highlightColor.g,
                           Kirigami.Theme.highlightColor.b,
                           liquidGlass ? 0.55 : 0.45);
        }
        return Qt.rgba(Kirigami.Theme.textColor.r,
                       Kirigami.Theme.textColor.g,
                       Kirigami.Theme.textColor.b,
                       liquidGlass ? 0.16 : 0.12);
    }

    // Top-to-bottom subtle refractive luminance sheen (macOS Tahoe / Golden Gate Liquid Glass)
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: Math.max(0, glassRoot.radius - 1)
        visible: glassRoot.liquidGlass
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: Qt.rgba(1.0, 1.0, 1.0, glassRoot.isHighlighted ? 0.16 : 0.10)
            }
            GradientStop {
                position: 0.35
                color: Qt.rgba(1.0, 1.0, 1.0, 0.02)
            }
            GradientStop {
                position: 1.0
                color: Qt.rgba(0.0, 0.0, 0.0, 0.06)
            }
        }
        border.width: 1
        border.color: Qt.rgba(1.0, 1.0, 1.0, glassRoot.isHighlighted ? 0.22 : 0.12)
    }
}
