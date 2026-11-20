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

    property var mathResults: []

    function focusInput(): void {
        mathInput.forceActiveFocus();
    }

    Layout.fillWidth: true
    Layout.preferredHeight: 144
    radius: CachyTheme.radiusSmall
    color: CachyPalette.cardBackground
    border.width: 1
    border.color: CachyPalette.specularBorder

    Timer {
        id: mathDebounce
        interval: 350
        onTriggered: root.mathResults = Plasmoid.evaluateMathNotes(mathInput.text)
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: CachyTheme.spacingSmall
        spacing: CachyTheme.spacingSmall

        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: parent.width * 0.58

            QQC2.TextArea {
                id: mathInput
                font.family: "monospace"
                font.pixelSize: 12
                color: CachyPalette.text
                selectionColor: CachyTheme.accentColor
                selectedTextColor: "#FFFFFF"
                wrapMode: TextEdit.NoWrap
                placeholderTextColor: CachyPalette.textMuted
                placeholderText: i18n("precio = 12,5\ncantidad = 8\ntotal = precio * cantidad\n15% de total\n5 km to miles")
                onTextChanged: mathDebounce.restart()
            }
        }

        CachyDivider {
            Layout.fillHeight: true
        }

        Column {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: parent.width * 0.38
            spacing: 0

            Repeater {
                model: root.mathResults

                Text {
                    required property var modelData
                    width: parent.width
                    height: mathInput.font.pixelSize > 0 ? Math.round(mathInput.font.pixelSize * 1.5) : 18
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                    font.family: "monospace"
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: CachyTheme.accentColor
                    text: modelData.length > 0 ? ("= " + modelData) : ""
                }
            }
        }
    }
}
