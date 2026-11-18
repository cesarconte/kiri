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

    property string promptText: ""
    signal toolTriggered(string action, string customText)

    Layout.fillWidth: true
    implicitHeight: wtCol.implicitHeight + Kirigami.Units.smallSpacing * 2
    radius: Kirigami.Units.smallSpacing
    color: Kirigami.Theme.alternateBackgroundColor
    border.width: 1
    border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.14)

    ColumnLayout {
        id: wtCol
        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                source: "document-edit-symbolic"
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
                color: Kirigami.Theme.highlightColor
            }

            QQC2.Label {
                Layout.fillWidth: true
                font.bold: true
                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                text: root.promptText.trim().length > 0
                    ? i18n("Herramientas de Escritura (usando caja de texto)")
                    : i18n("Herramientas de Escritura (usando selección / portapapeles Wayland)")
                elide: Text.ElideRight
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: Kirigami.Units.smallSpacing
            rowSpacing: Kirigami.Units.smallSpacing
            uniformCellWidths: true

            Repeater {
                model: [
                    { action: "proofread", label: i18n("✓ Corregir") },
                    { action: "rewrite", label: i18n("↻ Reescribir") },
                    { action: "friendly", label: i18n("😊 Amigable") },
                    { action: "professional", label: i18n("💼 Profesional") },
                    { action: "concise", label: i18n("⚡ Conciso") },
                    { action: "summary", label: i18n("📝 Resumir") },
                    { action: "keypoints", label: i18n("• Puntos clave") },
                    { action: "table", label: i18n("⊞ Tabla") },
                    { action: "translate:en", label: i18n("🌐 → Inglés") },
                    { action: "translate:es", label: i18n("🌐 → Español") }
                ]

                QQC2.Button {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.label
                    font: Kirigami.Theme.smallFont
                    enabled: !Plasmoid.isBusy
                    onClicked: {
                        const customText = root.promptText.trim();
                        root.toolTriggered(modelData.action, customText);
                    }
                }
            }
        }
    }
}
