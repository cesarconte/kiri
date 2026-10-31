/*
    SPDX-FileCopyrightText: 2026 Kiri contributors

    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

PlasmoidItem {
    id: root

    hideOnWindowDeactivate: !Plasmoid.configuration.pin

    Plasmoid.icon: {
        const isColor = Plasmoid.configuration.panelIconStyle === "color";
        const tc = Kirigami.Theme.textColor;
        const isDarkTheme = (tc.r * 0.299 + tc.g * 0.587 + tc.b * 0.114) > 0.5;

        if (Plasmoid.state === "listening") {
            if (isColor) {
                return "kiri-listening";
            }
            return isDarkTheme ? "kiri-listening-light" : "kiri-listening-dark";
        }
        if (isColor) {
            return "kiri";
        }
        return isDarkTheme ? "kiri-light" : "kiri-dark";
    }

    Plasmoid.status: PlasmaCore.Types.ActiveStatus

    toolTipMainText: i18n("Kiri")
    toolTipSubText: {
        if (Plasmoid.state === "listening") {
            return i18n("Listening to your voice…");
        } else if (Plasmoid.state === "transcribing") {
            return i18n("Transcribing speech…");
        } else if (Plasmoid.state === "thinking") {
            return i18n("Thinking…");
        } else if (Plasmoid.state === "speaking") {
            return i18n("Speaking response…");
        }
        return i18n("Click to open Kiri or middle-click to speak immediately");
    }

    onExpandedChanged: {
        if (root.expanded) {
            if (Plasmoid.configuration.autoListenOnActivate === true && Plasmoid.whisperAvailable) {
                Plasmoid.startRecording();
            }
        } else {
            if (Plasmoid.isRecording) {
                Plasmoid.stopRecording();
            }
        }
    }

    Connections {
        target: Plasmoid
        function onWakeWordTriggered() {
            root.expanded = true;
        }
    }

    Plasmoid.contextualActions: [
        PlasmaCore.Action {
            text: Plasmoid.isRecording ? i18n("Stop Listening") : i18n("Listen by Voice")
            icon.name: "kiri-symbolic"
            onTriggered: {
                root.expanded = true;
                Plasmoid.toggleRecording();
            }
        },
        PlasmaCore.Action {
            text: i18n("Stop Speaking")
            icon.name: "media-playback-stop-symbolic"
            visible: Plasmoid.isSpeaking
            onTriggered: Plasmoid.stopSpeaking()
        },
        PlasmaCore.Action {
            text: i18n("Clear Conversation")
            icon.name: "edit-clear-history-symbolic"
            enabled: Plasmoid.messages.length > 0
            onTriggered: Plasmoid.clearHistory()
        }
    ]

    compactRepresentation: MouseArea {
        id: compactMouse
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
        hoverEnabled: true

        onClicked: mouse => {
            if (mouse.button === Qt.MiddleButton) {
                root.expanded = true;
                Plasmoid.toggleRecording();
            } else {
                root.expanded = !root.expanded;
            }
        }

        // Subtle glowing ring behind panel icon when listening or speaking
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height) * 0.92
            height: width
            radius: width / 2
            visible: Plasmoid.state === "listening" || Plasmoid.state === "speaking"
            color: Plasmoid.state === "listening" ? "#00e5ff" : "#00e676"
            opacity: 0.35
        }

        Kirigami.Icon {
            anchors.fill: parent
            source: Plasmoid.icon
            active: compactMouse.containsMouse
        }
    }

    fullRepresentation: ExpandedRepresentation {
        widget: root
    }
}
