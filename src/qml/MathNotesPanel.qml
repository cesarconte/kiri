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

    property var mathResults: []

    function focusInput(): void {
        mathInput.forceActiveFocus();
    }

    Layout.fillWidth: true
    Layout.preferredHeight: Kirigami.Units.gridUnit * 9
    radius: Kirigami.Units.smallSpacing
    color: Kirigami.Theme.alternateBackgroundColor
    border.width: 1
    border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.14)

    Timer {
        id: mathDebounce
        interval: 350
        onTriggered: root.mathResults = Plasmoid.evaluateMathNotes(mathInput.text)
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.smallSpacing

        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: parent.width * 0.58

            QQC2.TextArea {
                id: mathInput
                font.family: "monospace"
                wrapMode: TextEdit.NoWrap
                placeholderText: i18n("precio = 12,5\ncantidad = 8\ntotal = precio * cantidad\n15% de total\n5 km to miles")
                onTextChanged: mathDebounce.restart()
            }
        }

        Kirigami.Separator { Layout.fillHeight: true }

        Column {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: parent.width * 0.38
            spacing: 0

            Repeater {
                model: root.mathResults

                QQC2.Label {
                    required property var modelData
                    width: parent.width
                    height: mathInput.font.pixelSize > 0 ? Math.round(mathInput.font.pixelSize * 1.5) : Kirigami.Units.gridUnit
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                    font.family: "monospace"
                    color: Kirigami.Theme.highlightColor
                    text: modelData.length > 0 ? ("= " + modelData) : ""
                }
            }
        }
    }
}
