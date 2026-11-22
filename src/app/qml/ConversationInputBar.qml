/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import CachyOS.UI

RowLayout {
    id: inputBarRoot

    Layout.fillWidth: true
    spacing: 10

    property alias text: promptField.text
    signal openPhotoDialogRequested()
    signal openFileDialogRequested()
    signal toggleWritingToolsRequested()
    signal toggleVisualIntelligenceRequested()
    signal toggleMathNotesRequested()

    // Left: Circular [+] Button (Apple Guide q7ny3g54v9lk: Fotos, Archivos, Herramientas)
    Rectangle {
        Layout.preferredWidth: 34
        Layout.preferredHeight: 34
        radius: 17
        color: plusHover.hovered ? CachyTheme.cardBackgroundHover : CachyTheme.surfacePill
        border.width: 1
        border.color: CachyTheme.specularBorder

        HoverHandler { id: plusHover; cursorShape: Qt.PointingHandCursor }
        TapHandler {
            onTapped: plusMenu.popup()
        }

        Text {
            anchors.centerIn: parent
            text: "+"
            font.pixelSize: 18
            color: CachyPalette.text
        }

        CachyMenu {
            id: plusMenu

            CachyMenuItem {
                iconName: "image-x-generic"
                text: i18n("Fotos…")
                onTriggered: inputBarRoot.openPhotoDialogRequested()
            }

            CachyMenuItem {
                iconName: "document-open"
                text: i18n("Archivos…")
                onTriggered: inputBarRoot.openFileDialogRequested()
            }

            CachyMenuSeparator {}

            CachyMenuItem {
                iconName: "document-edit-symbolic"
                text: i18n("Herramientas de Escritura")
                onTriggered: inputBarRoot.toggleWritingToolsRequested()
            }

            CachyMenuItem {
                iconName: "camera-photo-symbolic"
                text: i18n("Inteligencia Visual (Capturar pantalla)")
                onTriggered: inputBarRoot.toggleVisualIntelligenceRequested()
            }

            CachyMenuItem {
                iconName: "accessories-calculator"
                text: i18n("Notas Matemáticas")
                onTriggered: inputBarRoot.toggleMathNotesRequested()
            }
        }
    }

    // Center: "Ask Kity" Pill Input Field
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 36
        radius: 18
        color: CachyTheme.surfacePill
        border.width: promptField.activeFocus ? 1.5 : 1
        border.color: promptField.activeFocus ? CachyTheme.accentColor : CachyTheme.specularBorder

        QQC2.TextField {
            id: promptField
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            focus: true
            color: CachyPalette.text
            placeholderTextColor: CachyPalette.textMuted
            font.pixelSize: 13
            background: null
            placeholderText: kityClient.isRecording
                ? i18n("Listening...")
                : i18n("Ask Kity")
            onAccepted: {
                if (text.trim().length > 0) {
                    kityClient.sendMessage(text.trim(), false);
                    text = "";
                }
            }
        }
    }

    // Right: Circular Microphone Button
    Rectangle {
        Layout.preferredWidth: 34
        Layout.preferredHeight: 34
        radius: 17
        color: kityClient.isRecording
            ? "#FF375F"
            : (micHover.hovered ? CachyTheme.cardBackgroundHover : CachyTheme.surfacePill)
        border.width: 1
        border.color: kityClient.isRecording ? "#FF375F" : CachyTheme.specularBorder

        HoverHandler { id: micHover; cursorShape: Qt.PointingHandCursor }
        TapHandler {
            onTapped: {
                if (kityClient.isSpeaking) {
                    kityClient.stopSpeaking();
                } else if (kityClient.state === "thinking") {
                    kityClient.stopGeneration();
                } else {
                    kityClient.toggleRecording();
                }
            }
        }

        CachyIcon {
            anchors.centerIn: parent
            size: 16
            name: "audio-input-microphone-symbolic"
            color: kityClient.isRecording ? "#FFFFFF" : CachyPalette.text
        }
    }
}
