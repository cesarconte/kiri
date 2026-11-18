/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import CachyOS.UI

Rectangle {
    id: viRoot

    Layout.fillWidth: true
    implicitHeight: viCol.implicitHeight + 16
    radius: CachyTheme.radiusMedium
    color: CachyTheme.cardBackground
    border.width: 1
    border.color: CachyTheme.specularBorder

    property string captureScope: "region"
    property string targetQuery: ""

    signal requestClose()
    signal requestHideWindow()
    signal queryConsumed()

    ColumnLayout {
        id: viCol
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Kirigami.Icon {
                source: "camera-photo-symbolic"
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                color: CachyTheme.accentColor
            }

            QQC2.Label {
                text: i18n("Inteligencia Visual & Live Text:")
                font.bold: true
                font.pixelSize: 12
                color: CachyTheme.textPrimary
            }

            Item { Layout.fillWidth: true }

            // Scope selector
            Repeater {
                model: [
                    { scope: "region", label: i18n("📐 Región") },
                    { scope: "activewindow", label: i18n("🪟 Ventana activa") },
                    { scope: "fullscreen", label: i18n("🖥️ Pantalla") }
                ]

                Rectangle {
                    required property var modelData
                    height: 26
                    implicitWidth: scopeText.implicitWidth + 16
                    radius: 13
                    color: viRoot.captureScope === modelData.scope
                        ? CachyTheme.withAlpha(CachyTheme.accentColor, 0.28)
                        : (scopeHover.hovered ? CachyTheme.cardBackgroundHover : "transparent")
                    border.width: 1
                    border.color: viRoot.captureScope === modelData.scope
                        ? CachyTheme.withAlpha(CachyTheme.accentColor, 0.6)
                        : CachyTheme.specularBorder

                    HoverHandler { id: scopeHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: viRoot.captureScope = modelData.scope
                    }

                    Text {
                        id: scopeText
                        anchors.centerIn: parent
                        text: modelData.label
                        font.pixelSize: 11
                        font.weight: viRoot.captureScope === modelData.scope ? Font.DemiBold : Font.Normal
                        color: viRoot.captureScope === modelData.scope ? CachyTheme.textPrimary : CachyTheme.textSecondary
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            CachyButton {
                text: i18n("📋 Extraer texto (Live Text)")
                onClicked: {
                    const customQ = viRoot.targetQuery.trim();
                    if (viRoot.captureScope === "activewindow") {
                        viRoot.requestHideWindow();
                    }
                    kityClient.runVisualIntelligence(viRoot.captureScope, "ocr", customQ);
                    viRoot.requestClose();
                }
            }

            CachyButton {
                text: i18n("🌐 Traducir captura")
                onClicked: {
                    const customQ = viRoot.targetQuery.trim();
                    if (viRoot.captureScope === "activewindow") {
                        viRoot.requestHideWindow();
                    }
                    kityClient.runVisualIntelligence(viRoot.captureScope, "translate", customQ);
                    viRoot.requestClose();
                }
            }

            CachyButton {
                text: i18n("🔍 Explicar / Analizar")
                primary: true
                onClicked: {
                    const customQ = viRoot.targetQuery.trim();
                    if (viRoot.captureScope === "activewindow") {
                        viRoot.requestHideWindow();
                    }
                    kityClient.runVisualIntelligence(viRoot.captureScope, "explain", customQ);
                    viRoot.requestClose();
                    if (customQ.length > 0) {
                        viRoot.queryConsumed();
                    }
                }
            }
        }
    }
}
