/*
 * SPDX-FileCopyrightText: 2026 Kity contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import CachyOS.UI
import org.kde.plasma.plasmoid

Rectangle {
    id: root

    property string promptText: ""
    signal toolTriggered(string action, string customText)

    Layout.fillWidth: true
    implicitHeight: wtCol.implicitHeight + CachyTheme.spacingSmall * 2
    radius: CachyTheme.radiusSmall
    color: CachyPalette.cardBackground
    border.width: 1
    border.color: CachyPalette.specularBorder

    ColumnLayout {
        id: wtCol
        anchors.fill: parent
        anchors.margins: CachyTheme.spacingSmall
        spacing: CachyTheme.spacingSmall

        RowLayout {
            Layout.fillWidth: true
            spacing: CachyTheme.spacingSmall

            CachyIcon {
                name: "document-edit-symbolic"
                size: 16
                color: CachyTheme.accentColor
            }

            Text {
                Layout.fillWidth: true
                font.bold: true
                font.pixelSize: 11
                color: CachyPalette.text
                text: root.promptText.trim().length > 0
                    ? i18n("Herramientas de Escritura (usando caja de texto)")
                    : i18n("Herramientas de Escritura (usando selección / portapapeles Wayland)")
                elide: Text.ElideRight
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: CachyTheme.spacingSmall
            rowSpacing: CachyTheme.spacingSmall
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

                CachyButton {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.label
                    font.pixelSize: 11
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
