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

    property string visualCaptureScope: "region"
    property string promptText: ""
    signal visualTriggered(string scope, string action, string customQuestion)

    Layout.fillWidth: true
    implicitHeight: viCol.implicitHeight + CachyTheme.spacingSmall * 2
    radius: CachyTheme.radiusSmall
    color: CachyPalette.cardBackground
    border.width: 1
    border.color: CachyPalette.specularBorder

    ColumnLayout {
        id: viCol
        anchors.fill: parent
        anchors.margins: CachyTheme.spacingSmall
        spacing: CachyTheme.spacingSmall

        RowLayout {
            Layout.fillWidth: true
            spacing: CachyTheme.spacingSmall

            CachyIcon {
                name: "camera-photo-symbolic"
                size: 16
                color: CachyTheme.accentColor
            }

            Text {
                font.bold: true
                font.pixelSize: 11
                color: CachyPalette.text
                text: i18n("Inteligencia Visual y Live Text:")
            }

            Item { Layout.fillWidth: true }

            Repeater {
                model: [
                    { scope: "region", label: i18n("Región") },
                    { scope: "activewindow", label: i18n("Ventana") },
                    { scope: "fullscreen", label: i18n("Pantalla") }
                ]

                CachyToolbarButton {
                    required property var modelData
                    text: modelData.label
                    checkable: true
                    checked: root.visualCaptureScope === modelData.scope
                    onToggled: root.visualCaptureScope = modelData.scope
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: CachyTheme.spacingSmall
            rowSpacing: CachyTheme.spacingSmall
            uniformCellWidths: true

            Repeater {
                model: [
                    { action: "ocr", label: i18n("Extraer texto (Live Text)"), icon: "edit-copy" },
                    { action: "translate", label: i18n("Traducir captura"), icon: "globe" },
                    { action: "explain", label: i18n("Explicar / Analizar"), icon: "search" }
                ]

                CachyButton {
                    required property var modelData
                    Layout.fillWidth: true
                    iconName: modelData.icon
                    text: modelData.label
                    font.pixelSize: 11
                    enabled: !Plasmoid.isBusy
                    onClicked: {
                        const customQ = root.promptText.trim();
                        root.visualTriggered(root.visualCaptureScope, modelData.action, customQ);
                    }
                }
            }
        }
    }
}
