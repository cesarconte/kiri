/*
    SPDX-FileCopyrightText: 2026 Kity contributors

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

    preferredRepresentation: PlasmoidItem.CompactRepresentation

    Plasmoid.icon: {
        const isColor = Plasmoid.configuration.panelIconStyle === "color";
        if (Plasmoid.state === "listening") {
            return isColor ? "kity-listening" : "kity-listening-symbolic";
        }
        return isColor ? "kity" : "kity-symbolic";
    }

    Plasmoid.status: PlasmaCore.Types.ActiveStatus
    Plasmoid.backgroundHints: PlasmaCore.Types.DefaultBackground

    toolTipMainText: i18n("Kity")
    toolTipSubText: {
        if (Plasmoid.state === "listening") {
            return i18n("Escuchando tu voz…");
        } else if (Plasmoid.state === "transcribing") {
            return i18n("Transcribiendo voz…");
        } else if (Plasmoid.state === "thinking") {
            return i18n("Pensando…");
        } else if (Plasmoid.state === "speaking") {
            return i18n("Reproduciendo respuesta…");
        }
        return i18n("Haz clic para abrir Kity o clic central para hablar de inmediato");
    }

    Plasmoid.contextualActions: [
        PlasmaCore.Action {
            text: i18n("Abrir / Ocultar Kity")
            icon.name: "kity"
            onTriggered: Plasmoid.toggleUI()
        },
        PlasmaCore.Action {
            text: Plasmoid.isRecording ? i18n("Detener escucha") : i18n("Hablar por voz")
            icon.name: "kity-symbolic"
            onTriggered: {
                Plasmoid.openUI();
                Plasmoid.toggleRecording();
            }
        },
        PlasmaCore.Action {
            text: i18n("Detener lectura en voz alta")
            icon.name: "media-playback-stop-symbolic"
            visible: Plasmoid.isSpeaking
            onTriggered: Plasmoid.stopSpeaking()
        },
        PlasmaCore.Action {
            text: i18n("Limpiar conversación")
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
                Plasmoid.openUI();
                Plasmoid.toggleRecording();
            } else {
                Plasmoid.toggleUI();
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
}
