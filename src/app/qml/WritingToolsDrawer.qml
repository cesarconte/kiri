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
    id: wtRoot

    Layout.fillWidth: true
    implicitHeight: wtCol.implicitHeight + 16
    radius: CachyTheme.radiusMedium
    color: CachyTheme.cardBackground
    border.width: 1
    border.color: CachyTheme.specularBorder

    signal requestClose()
    property string targetText: ""

    ColumnLayout {
        id: wtCol
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        QQC2.Label {
            Layout.fillWidth: true
            font.bold: true
            font.pixelSize: 12
            color: CachyPalette.text
            text: i18n("✍️ Writing Tools (Apple Intelligence / Golden Gate)")
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 5
            columnSpacing: 6
            rowSpacing: 6

            Repeater {
                model: [
                    { label: i18n("✓ Corregir"), action: "proofread" },
                    { label: i18n("↻ Reescribir"), action: "rewrite" },
                    { label: i18n("💼 Profesional"), action: "professional" },
                    { label: i18n("😊 Amigable"), action: "friendly" },
                    { label: i18n("⚡ Conciso"), action: "concise" },
                    { label: i18n("📝 Resumen"), action: "summary" },
                    { label: i18n("• Puntos clave"), action: "keypoints" },
                    { label: i18n("📊 Tabla"), action: "table" },
                    { label: i18n("🌐 A Español"), action: "translate:es" },
                    { label: i18n("🌐 A Inglés"), action: "translate:en" }
                ]

                CachyButton {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.label
                    onClicked: {
                        kityClient.applyWritingTool(modelData.action, wtRoot.targetText);
                        wtRoot.requestClose();
                    }
                }
            }
        }
    }
}
