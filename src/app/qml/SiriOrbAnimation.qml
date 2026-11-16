/*
    SPDX-FileCopyrightText: 2026 Kity contributors

    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: orbRoot

    property string assistantState: "idle"
    property real audioLevel: 0.0
    property int customVoiceIndex: -1
    signal clicked()

    readonly property bool isVoiceActive: assistantState === "listening" || assistantState === "speaking"

    onAssistantStateChanged: {
        if (assistantState !== "thinking" && assistantState !== "transcribing") {
            iconItem.rotation = 0;
        }
    }

    implicitWidth: Kirigami.Units.gridUnit * 4.0
    implicitHeight: Kirigami.Units.gridUnit * 4.0
    width: implicitWidth
    height: implicitHeight

    readonly property var voicePalettePrimary: [
        "#00e5ff", // 0 Cyan
        "#5c6bc0", // 1 Indigo
        "#ab47bc", // 2 Violet
        "#ff2d55", // 3 Red/Pink (Apple Siri Signature)
        "#ff9500", // 4 Amber/Orange
        "#34c759", // 5 Emerald/Green
        "#00f5ff"  // 6 Apple Intelligence Multichromatic
    ]

    readonly property var voicePaletteSecondary: [
        "#007aff", // 0 Blue
        "#7e57c2", // 1 Deep Purple
        "#ec407a", // 2 Magenta
        "#ff6482", // 3 Coral Pink
        "#ffcc00", // 4 Warm Gold
        "#30d158", // 5 Mint
        "#ff2d55"  // 6 Hot Magenta
    ]

    readonly property color primaryColor: {
        if (assistantState === "listening") {
            return "#00e5ff";
        } else if (assistantState === "thinking" || assistantState === "transcribing") {
            return "#b388ff";
        } else if (assistantState === "speaking") {
            return "#00e676";
        } else if (assistantState === "error") {
            return Kirigami.Theme.negativeTextColor;
        }
        let vIdx = customVoiceIndex >= 0 ? customVoiceIndex : 3;
        if (vIdx >= 0 && vIdx < voicePalettePrimary.length) {
            return voicePalettePrimary[vIdx];
        }
        return Kirigami.Theme.highlightColor;
    }

    readonly property color secondaryColor: {
        if (assistantState === "listening") {
            return "#ff4081";
        } else if (assistantState === "thinking" || assistantState === "transcribing") {
            return "#448aff";
        } else if (assistantState === "speaking") {
            return "#18ffff";
        }
        let vIdx = customVoiceIndex >= 0 ? customVoiceIndex : 3;
        if (vIdx >= 0 && vIdx < voicePaletteSecondary.length) {
            return voicePaletteSecondary[vIdx];
        }
        return Qt.lighter(Kirigami.Theme.highlightColor, 1.4);
    }

    readonly property bool activeAnimation: assistantState !== "idle" && assistantState !== "error"

    // Outer pulsing aura ring (fixed geometry, animated scale & opacity)
    Rectangle {
        id: outerAura
        anchors.centerIn: parent
        width: parent.width * 0.82
        height: width
        radius: width / 2
        color: orbRoot.secondaryColor
        opacity: orbRoot.activeAnimation
            ? (0.35 + (orbRoot.isVoiceActive ? Math.min(0.45, orbRoot.audioLevel * 0.5) : 0.0))
            : 0.12

        Behavior on opacity {
            NumberAnimation { duration: 180 }
        }

        SequentialAnimation on scale {
            running: orbRoot.activeAnimation
            loops: Animation.Infinite
            NumberAnimation {
                from: 0.90
                to: 1.15
                duration: orbRoot.isVoiceActive ? 420 : 1000
                easing.type: Easing.InOutSine
            }
            NumberAnimation {
                from: 1.15
                to: 0.90
                duration: orbRoot.isVoiceActive ? 420 : 1000
                easing.type: Easing.InOutSine
            }
        }
    }

    // Middle glowing wave ring (fixed geometry, animated scale & opacity)
    Rectangle {
        id: midRing
        anchors.centerIn: parent
        width: parent.width * 0.70
        height: width
        radius: width / 2
        border.width: 1.5 + (orbRoot.isVoiceActive ? orbRoot.audioLevel * 2.5 : 0.0)
        border.color: orbRoot.primaryColor
        color: "transparent"
        opacity: orbRoot.activeAnimation
            ? (0.65 + (orbRoot.isVoiceActive ? Math.min(0.35, orbRoot.audioLevel * 0.4) : 0.0))
            : 0.25

        Behavior on opacity {
            NumberAnimation { duration: 180 }
        }

        SequentialAnimation on scale {
            running: orbRoot.activeAnimation
            loops: Animation.Infinite
            NumberAnimation {
                from: 0.88
                to: 1.08
                duration: orbRoot.isVoiceActive ? 520 : 800
                easing.type: Easing.InOutQuad
            }
            NumberAnimation {
                from: 1.08
                to: 0.88
                duration: orbRoot.isVoiceActive ? 520 : 800
                easing.type: Easing.InOutQuad
            }
        }
    }

    // Core Siri sphere
    Rectangle {
        id: coreOrb
        anchors.centerIn: parent
        width: parent.width * 0.58
        height: width
        radius: width / 2
        scale: (mouseArea.pressed ? 0.92 : (mouseArea.containsMouse ? 1.06 : 1.0))
            + (orbRoot.isVoiceActive ? Math.min(0.38, orbRoot.audioLevel * 0.35) : 0.0)

        Behavior on scale {
            NumberAnimation { duration: 120; easing.type: Easing.OutQuad }
        }

        gradient: Gradient {
            GradientStop { position: 0.0; color: orbRoot.primaryColor }
            GradientStop { position: 1.0; color: orbRoot.secondaryColor }
        }

        // Specular Liquid Glass lens highlight on top hemisphere
        Rectangle {
            anchors.top: parent.top
            anchors.topMargin: parent.height * 0.06
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width * 0.72
            height: parent.height * 0.42
            radius: height / 2
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1.0, 1.0, 1.0, 0.42) }
                GradientStop { position: 1.0; color: Qt.rgba(1.0, 1.0, 1.0, 0.0) }
            }
        }

        border.width: 1
        border.color: Qt.rgba(1.0, 1.0, 1.0, 0.35)

        Kirigami.Icon {
            id: iconItem
            anchors.centerIn: parent
            width: parent.width * 0.52
            height: width
            color: "#ffffff"
            isMask: true
            rotation: 0
            source: {
                if (orbRoot.assistantState === "listening") {
                    return "media-playback-stop-symbolic";
                } else if (orbRoot.assistantState === "speaking") {
                    return "audio-volume-high-symbolic";
                } else if (orbRoot.assistantState === "thinking" || orbRoot.assistantState === "transcribing") {
                    return "view-refresh-symbolic";
                }
                return "audio-input-microphone-symbolic";
            }

            RotationAnimation {
                id: spinAnim
                target: iconItem
                property: "rotation"
                running: orbRoot.assistantState === "thinking" || orbRoot.assistantState === "transcribing"
                loops: Animation.Infinite
                from: 0
                to: 360
                duration: 1200
                onRunningChanged: {
                    if (!running) {
                        iconItem.rotation = 0;
                    }
                }
            }
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: orbRoot.clicked()
    }
}
