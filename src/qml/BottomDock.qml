/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.plasmoid

Rectangle {
    id: root

    property alias promptText: promptField.text

    signal messageSubmitted(string text)

    function focusInput(): void {
        promptField.forceActiveFocus();
    }

    Layout.fillWidth: true
    implicitHeight: dockCol.implicitHeight + Kirigami.Units.mediumSpacing * 2
    radius: Kirigami.Units.smallSpacing
    color: (Plasmoid.state === "listening")
        ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.14)
        : Kirigami.Theme.backgroundColor
    border.width: 1
    border.color: (Plasmoid.state === "listening")
        ? Kirigami.Theme.highlightColor
        : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.14)

    ColumnLayout {
        id: dockCol
        anchors.fill: parent
        anchors.topMargin: Kirigami.Units.mediumSpacing
        anchors.bottomMargin: Kirigami.Units.mediumSpacing
        anchors.leftMargin: Kirigami.Units.mediumSpacing
        anchors.rightMargin: Kirigami.Units.mediumSpacing
        spacing: Kirigami.Units.mediumSpacing

        // Siri Orb Centered Interactive Voice Control
        Item {
            Layout.fillWidth: true
            implicitHeight: siriOrb.implicitHeight + Kirigami.Units.smallSpacing * 2
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
            implicitHeight: Math.max(promptInputRow.implicitHeight + Kirigami.Units.smallSpacing * 2, Kirigami.Units.gridUnit * 2.5)
            radius: 12
            color: palette.base
            border.width: promptField.activeFocus ? 1.5 : 1
            border.color: promptField.activeFocus
                ? palette.highlight
                : Kirigami.ColorUtils.linearInterpolation(palette.base, palette.text, 0.18)

            Behavior on border.color { ColorAnimation { duration: 150 } }

            RowLayout {
                id: promptInputRow
                anchors.fill: parent
                anchors.leftMargin: Kirigami.Units.mediumSpacing
                anchors.rightMargin: Kirigami.Units.smallSpacing
                anchors.topMargin: Kirigami.Units.smallSpacing
                anchors.bottomMargin: Kirigami.Units.smallSpacing
                spacing: Kirigami.Units.smallSpacing

                QQC2.ScrollView {
                    id: promptScrollView
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    Layout.maximumHeight: Kirigami.Units.gridUnit * 6
                    QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

                    QQC2.TextArea {
                        id: promptField
                        focus: true
                        color: palette.text
                        placeholderTextColor: palette.placeholderText
                        selectionColor: palette.highlight
                        selectedTextColor: palette.highlightedText
                        font: Kirigami.Theme.defaultFont
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

                QQC2.Button {
                    id: expSendBtn
                    Layout.alignment: Qt.AlignBottom
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 2.2
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 2.2
                    hoverEnabled: true
                    enabled: Plasmoid.state === "thinking" || promptField.text.trim().length > 0

                    contentItem: Kirigami.Icon {
                        anchors.centerIn: parent
                        source: Plasmoid.state === "thinking" ? "process-stop-symbolic" : "document-send-symbolic"
                        color: expSendBtn.enabled
                            ? (Plasmoid.state === "thinking" ? Kirigami.Theme.negativeTextColor : (expSendBtn.hovered ? palette.highlight : palette.buttonText))
                            : palette.placeholderText
                    }

                    background: Rectangle {
                        radius: 8
                        color: expSendBtn.down
                            ? Kirigami.ColorUtils.tintWithAlpha(palette.button, palette.highlight, 0.25)
                            : (expSendBtn.hovered
                                ? Kirigami.ColorUtils.tintWithAlpha(palette.button, palette.highlight, 0.12)
                                : (expSendBtn.enabled ? palette.button : "transparent"))
                        border.width: 1
                        border.color: expSendBtn.enabled
                            ? (expSendBtn.hovered ? palette.highlight : Kirigami.ColorUtils.linearInterpolation(palette.window, palette.windowText, 0.15))
                            : "transparent"
                    }

                    onClicked: {
                        if (Plasmoid.state === "thinking") {
                            Plasmoid.stopGeneration();
                        } else if (promptField.text.trim().length > 0) {
                            root.messageSubmitted(promptField.text);
                            promptField.text = "";
                        }
                    }
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: Plasmoid.state === "thinking" ? i18n("Detener generación") : i18n("Enviar mensaje")
                }
            }
        }
    }
}
