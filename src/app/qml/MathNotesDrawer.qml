/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import CachyOS.UI

Rectangle {
    id: mathRoot

    Layout.fillWidth: true
    implicitHeight: 220
    radius: CachyTheme.radiusMedium
    color: CachyTheme.cardBackground
    border.width: 1
    border.color: CachyTheme.specularBorder

    property var mathResults: []

    Timer {
        id: mathDebounce
        interval: 250
        onTriggered: {
            if (mathTextArea.text.trim().length > 0) {
                mathRoot.mathResults = kityClient.evaluateMathNotes(mathTextArea.text);
            } else {
                mathRoot.mathResults = [];
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            CachyIcon {
                name: "view-time-schedule-calculus-symbolic"
                size: 16
                color: CachyTheme.accentColor
            }

            Text {
                text: i18n("Notas Matemáticas & Scratchpad:")
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: CachyTheme.textPrimary
            }

            Text {
                text: i18n("(evaluación reactiva, variables persistentes, unidades y divisas)")
                font.pixelSize: 11
                color: CachyTheme.textSecondary
            }

            Item { Layout.fillWidth: true }

            // Action Buttons: Copy Results & Clear
            Rectangle {
                height: 24
                implicitWidth: copyMathRow.implicitWidth + 14
                radius: 12
                color: copyMathHover.hovered ? CachyTheme.cardBackgroundHover : CachyTheme.surfacePill
                border.width: 1
                border.color: CachyTheme.specularBorder

                HoverHandler { id: copyMathHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        const lines = mathTextArea.text.split("\n");
                        let combined = [];
                        for (let i = 0; i < lines.length; ++i) {
                            const r = (mathRoot.mathResults && i < mathRoot.mathResults.length) ? mathRoot.mathResults[i] : "";
                            if (r && r.length > 0) {
                                combined.push(lines[i] + " = " + r);
                            } else {
                                combined.push(lines[i]);
                            }
                        }
                        kityClient.copyToClipboard(combined.join("\n"));
                    }
                }

                RowLayout {
                    id: copyMathRow
                    anchors.centerIn: parent
                    spacing: 4

                    CachyIcon {
                        name: "edit-copy-symbolic"
                        size: 12
                        color: CachyTheme.textSecondary
                    }

                    Text {
                        text: i18n("Copiar cálculos")
                        font.pixelSize: 11
                        color: CachyTheme.textPrimary
                    }
                }
            }

            Rectangle {
                height: 24
                implicitWidth: clearMathRow.implicitWidth + 14
                radius: 12
                color: clearMathHover.hovered ? CachyTheme.cardBackgroundHover : CachyTheme.surfacePill
                border.width: 1
                border.color: CachyTheme.specularBorder

                HoverHandler { id: clearMathHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        mathTextArea.text = "";
                        mathRoot.mathResults = [];
                    }
                }

                RowLayout {
                    id: clearMathRow
                    anchors.centerIn: parent
                    spacing: 4

                    CachyIcon {
                        name: "edit-clear-all-symbolic"
                        size: 12
                        color: CachyTheme.textSecondary
                    }

                    Text {
                        text: i18n("Limpiar")
                        font.pixelSize: 11
                        color: CachyTheme.textPrimary
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 8
            color: CachyTheme.surfacePill
            border.width: 1
            border.color: CachyTheme.specularBorder

            RowLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 8

                // Left: Editor
                QQC2.ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: parent.width * 0.62

                    QQC2.TextArea {
                        id: mathTextArea
                        font.family: "monospace"
                        font.pixelSize: 12
                        wrapMode: TextEdit.NoWrap
                        color: CachyTheme.textPrimary
                        placeholderTextColor: CachyTheme.textMuted
                        placeholderText: i18n("precio = 12,5\ncantidad = 8\ntotal = precio * cantidad\n15% de total\n100 dolares a euros\n5 km a millas\n25 celsius a fahrenheit")
                        onTextChanged: mathDebounce.restart()
                        background: null
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: CachyTheme.specularBorder
                }

                // Right: Results Column
                QQC2.ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: parent.width * 0.38

                    ColumnLayout {
                        width: parent.width
                        spacing: 0

                        Repeater {
                            model: mathRoot.mathResults

                            Item {
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                height: mathTextArea.font.pixelSize > 0 ? Math.round(mathTextArea.font.pixelSize * 1.55) : 19

                                Text {
                                    anchors.fill: parent
                                    anchors.rightMargin: 8
                                    verticalAlignment: Text.AlignVCenter
                                    horizontalAlignment: Text.AlignRight
                                    elide: Text.ElideRight
                                    font.family: "monospace"
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    color: CachyTheme.accentColor
                                    text: (modelData && modelData.length > 0) ? ("= " + modelData) : ""
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
