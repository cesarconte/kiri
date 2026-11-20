/*
 * SPDX-FileCopyrightText: 2026 Kity contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import CachyOS.UI
import org.kde.plasma.plasmoid

Rectangle {
    id: root

    property alias promptText: promptField.text

    signal messageSubmitted(string text)

    function focusInput(): void {
        promptField.forceActiveFocus();
    }

    Layout.fillWidth: true
    implicitHeight: dockCol.implicitHeight + CachyTheme.spacingMedium * 2
    radius: CachyTheme.radiusSmall
    color: (Plasmoid.state === "listening")
        ? Qt.rgba(CachyTheme.accentColor.r, CachyTheme.accentColor.g, CachyTheme.accentColor.b, 0.14)
        : CachyPalette.cardBackground
    border.width: 1
    border.color: (Plasmoid.state === "listening")
        ? CachyTheme.accentColor
        : CachyPalette.specularBorder

    ColumnLayout {
        id: dockCol
        anchors.fill: parent
        anchors.margins: CachyTheme.spacingMedium
        spacing: CachyTheme.spacingMedium

        // Siri Orb Centered Interactive Voice Control
        Item {
            Layout.fillWidth: true
            implicitHeight: siriOrb.implicitHeight + CachyTheme.spacingSmall * 2
            clip: false

            SiriOrbAnimation {
                id: siriOrb
                anchors.centerIn: parent
                assistantState: Plasmoid.state
                audioLevel: Plasmoid.audioLevel
                onClicked: {
                    if (Plasmoid.isSpeaking) {
                        Plasmoid.stopSpeaking();
                    } else if (Plasmoid.state === "thinking") {
                        Plasmoid.stopGeneration();
                    } else {
                        Plasmoid.toggleRecording();
                    }
                }
            }
        }

        // Bottom Text Input & Send Bar
        Rectangle {
            id: promptInputBox
            Layout.fillWidth: true
            implicitHeight: Math.max(promptInputRow.implicitHeight + CachyTheme.spacingSmall * 2, 40)
            radius: 12
            color: CachyPalette.isDark ? Qt.rgba(0, 0, 0, 0.32) : Qt.rgba(0, 0, 0, 0.06)
            border.width: promptField.activeFocus ? 1.5 : 1
            border.color: promptField.activeFocus
                ? CachyTheme.accentColor
                : CachyPalette.specularBorder

            Behavior on border.color { ColorAnimation { duration: 150 } }

            RowLayout {
                id: promptInputRow
                anchors.fill: parent
                anchors.leftMargin: CachyTheme.spacingMedium
                anchors.rightMargin: CachyTheme.spacingSmall
                anchors.topMargin: CachyTheme.spacingSmall
                anchors.bottomMargin: CachyTheme.spacingSmall
                spacing: CachyTheme.spacingSmall

                QQC2.ScrollView {
                    id: promptScrollView
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    Layout.maximumHeight: 96
                    QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

                    QQC2.TextArea {
                        id: promptField
                        focus: true
                        color: CachyPalette.text
                        placeholderTextColor: CachyPalette.textMuted
                        selectionColor: CachyTheme.accentColor
                        selectedTextColor: "#FFFFFF"
                        font.pixelSize: 13
                        background: null
                        placeholderText: Plasmoid.isRecording
                            ? i18n("Escuchando tu voz…")
                            : i18n("Pregúntale o pídele cualquier cosa a Kity…")
                        enabled: !Plasmoid.isRecording
                        wrapMode: TextEdit.Wrap

                        Keys.onPressed: event => {
                            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ShiftModifier)) {
                                if (Plasmoid.state !== "thinking" && text.trim().length > 0) {
                                    root.messageSubmitted(text);
                                    text = "";
                                }
                                event.accepted = true;
                            }
                        }
                    }
                }

                CachyToolbarButton {
                    id: expSendBtn
                    Layout.alignment: Qt.AlignBottom
                    implicitWidth: 32
                    implicitHeight: 32
                    enabled: Plasmoid.state === "thinking" || promptField.text.trim().length > 0
                    primary: enabled && Plasmoid.state !== "thinking"
                    iconName: Plasmoid.state === "thinking" ? "process-stop-symbolic" : "document-send-symbolic"
                    tooltipText: Plasmoid.state === "thinking" ? i18n("Detener generación") : i18n("Enviar mensaje")
                    onClicked: {
                        if (Plasmoid.state === "thinking") {
                            Plasmoid.stopGeneration();
                        } else if (promptField.text.trim().length > 0) {
                            root.messageSubmitted(promptField.text);
                            promptField.text = "";
                        }
                    }
                }
            }
        }
    }
}
