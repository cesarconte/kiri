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

            CachyIcon {
                name: "camera-photo-symbolic"
                size: 16
                color: CachyTheme.accentColor
            }

            QQC2.Label {
                text: i18n("Inteligencia Visual & Live Text:")
                font.bold: true
                font.pixelSize: 12
                color: CachyPalette.text
            }

            Item { Layout.fillWidth: true }

            // Scope selector
            CachySegmentedControl {
                model: [ i18n("📐 Región"), i18n("🪟 Ventana activa"), i18n("🖥️ Pantalla") ]
                currentIndex: viRoot.captureScope === "region" ? 0 : (viRoot.captureScope === "activewindow" ? 1 : 2)
                accentSelected: true
                onCurrentIndexChanged: {
                    if (currentIndex === 0) viRoot.captureScope = "region";
                    else if (currentIndex === 1) viRoot.captureScope = "activewindow";
                    else viRoot.captureScope = "fullscreen";
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
