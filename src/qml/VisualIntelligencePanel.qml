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

    property string visualCaptureScope: "region"
    property string promptText: ""
    signal visualTriggered(string scope, string action, string customQuestion)

    Layout.fillWidth: true
    implicitHeight: viCol.implicitHeight + Kirigami.Units.smallSpacing * 2
    radius: Kirigami.Units.smallSpacing
    color: Kirigami.Theme.alternateBackgroundColor
    border.width: 1
    border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.14)

    ColumnLayout {
        id: viCol
        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                source: "camera-photo-symbolic"
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
                color: Kirigami.Theme.highlightColor
            }

            QQC2.Label {
                font.bold: true
                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                text: i18n("Inteligencia Visual y Live Text:")
            }

            Item { Layout.fillWidth: true }

            Repeater {
                model: [
                    { scope: "region", label: i18n("📐 Región") },
                    { scope: "activewindow", label: i18n("🪟 Ventana") },
                    { scope: "fullscreen", label: i18n("🖥️ Pantalla") }
                ]

                QQC2.ToolButton {
                    required property var modelData
                    text: modelData.label
                    font: Kirigami.Theme.smallFont
                    checkable: true
                    checked: root.visualCaptureScope === modelData.scope
                    onClicked: root.visualCaptureScope = modelData.scope
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: Kirigami.Units.smallSpacing
            rowSpacing: Kirigami.Units.smallSpacing
            uniformCellWidths: true

            Repeater {
                model: [
                    { action: "ocr", label: i18n("📋 Extraer texto (Live Text)") },
                    { action: "translate", label: i18n("🌐 Traducir captura") },
                    { action: "explain", label: i18n("🔍 Explicar / Analizar") }
                ]

                QQC2.Button {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.label
                    font: Kirigami.Theme.smallFont
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
