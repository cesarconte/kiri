/*
    SPDX-FileCopyrightText: 2026 Kiri contributors

    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: orbRoot

    property string assistantState: "idle"
    property real audioLevel: 0.0
    signal clicked()

    onAssistantStateChanged: {
        if (assistantState !== "thinking" && assistantState !== "transcribing") {
            iconItem.rotation = 0;
        }
    }

    implicitWidth: Kirigami.Units.gridUnit * 3.8
    implicitHeight: Kirigami.Units.gridUnit * 3.8

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
        return Qt.lighter(Kirigami.Theme.highlightColor, 1.4);
    }

    readonly property bool activeAnimation: assistantState !== "idle" && assistantState !== "error"

    // Outer pulsing aura ring (fixed geometry, animated scale & opacity)
    Rectangle {
        id: outerAura
        anchors.centerIn: parent
        width: parent.width * 0.94
        height: width
        radius: width / 2
        color: orbRoot.secondaryColor
        opacity: orbRoot.activeAnimation ? 0.35 : 0.12

        Behavior on opacity {
            NumberAnimation { duration: 250 }
        }

        SequentialAnimation on scale {
            running: orbRoot.activeAnimation
            loops: Animation.Infinite
            NumberAnimation {
                from: 0.94
                to: 1.15
                duration: orbRoot.assistantState === "listening" ? 500 : 1100
                easing.type: Easing.InOutSine
            }
            NumberAnimation {
                from: 1.15
                to: 0.94
                duration: orbRoot.assistantState === "listening" ? 500 : 1100
                easing.type: Easing.InOutSine
            }
        }
    }

    // Middle glowing wave ring (fixed geometry, animated scale & opacity)
    Rectangle {
        id: midRing
        anchors.centerIn: parent
        width: parent.width * 0.76
        height: width
        radius: width / 2
        border.width: 2
        border.color: orbRoot.primaryColor
        color: "transparent"
        opacity: orbRoot.activeAnimation ? 0.75 : 0.25

        Behavior on opacity {
            NumberAnimation { duration: 250 }
        }

        SequentialAnimation on scale {
            running: orbRoot.activeAnimation
            loops: Animation.Infinite
            NumberAnimation {
                from: 0.88
                to: 1.08
                duration: 750
                easing.type: Easing.InOutQuad
            }
            NumberAnimation {
                from: 1.08
                to: 0.88
                duration: 750
                easing.type: Easing.InOutQuad
            }
        }
    }

    // Core Siri sphere
    Rectangle {
        id: coreOrb
        anchors.centerIn: parent
        width: parent.width * 0.62
        height: width
        radius: width / 2
        scale: (mouseArea.pressed ? 0.92 : (mouseArea.containsMouse ? 1.06 : 1.0))
            + (orbRoot.assistantState === "listening" ? orbRoot.audioLevel * 0.18 : 0.0)

        Behavior on scale {
            NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
        }

        gradient: Gradient {
            GradientStop { position: 0.0; color: orbRoot.primaryColor }
            GradientStop { position: 1.0; color: orbRoot.secondaryColor }
        }

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
