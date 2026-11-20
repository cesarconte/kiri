/*
 * SPDX-FileCopyrightText: 2026 Kity contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import CachyOS.UI

Rectangle {
    id: root
    property var cardData: ({})

    implicitHeight: mainLayout.implicitHeight + 16
    radius: CachyTheme.radiusMedium
    color: Qt.rgba(0.08, 0.10, 0.14, 0.88)
    border.color: CachyPalette.specularBorder
    border.width: 1

    // Dynamic weather atmosphere gradient tint
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        opacity: 0.18
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop {
                position: 0.0
                color: {
                    const code = Number(root.cardData.wmoCode || 0);
                    if (code === 0 || code === 1) return "#ff9800"; // Sol
                    if (code >= 51 && code <= 67) return "#0288d1"; // Lluvia
                    if (code >= 71 && code <= 86) return "#b0bec5"; // Nieve
                    if (code >= 95) return "#7b1fa2"; // Tormenta
                    return "#00bcd4"; // Nuboso por defecto
                }
            }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    ColumnLayout {
        id: mainLayout
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            margins: CachyTheme.spacingMedium
        }
        spacing: CachyTheme.spacingSmall

        // 1. Header (Location & Badge)
        RowLayout {
            Layout.fillWidth: true
            spacing: CachyTheme.spacingSmall

            CachyIcon {
                name: "mark-location"
                size: 16
                color: CachyTheme.accentColor
            }

            Text {
                text: root.cardData.location || root.cardData.city || i18n("Tiempo actual")
                font.weight: Font.DemiBold
                font.pixelSize: 13
                color: CachyPalette.text
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Rectangle {
                implicitWidth: liveLabel.implicitWidth + 12
                implicitHeight: liveLabel.implicitHeight + 4
                radius: 8
                color: Qt.rgba(0.0, 0.8, 0.4, 0.2)
                border.color: "#00e676"
                border.width: 1

                Text {
                    id: liveLabel
                    anchors.centerIn: parent
                    text: root.cardData.isForecast ? (root.cardData.forecastPeriod ? root.cardData.forecastPeriod.toUpperCase() : i18n("PREVISIÓN")) : i18n("EN VIVO")
                    font.pixelSize: 10
                    font.weight: Font.Bold
                    color: root.cardData.isForecast ? "#00bcd4" : "#00e676"
                }
            }
        }

        // 2. Main Temperature & Weather Icon
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: CachyTheme.spacingSmall
            Layout.bottomMargin: CachyTheme.spacingSmall
            spacing: CachyTheme.spacingMedium

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                RowLayout {
                    spacing: 4
                    Text {
                        text: (root.cardData.temp !== undefined ? Math.round(Number(root.cardData.temp)) : "--") + "°"
                        font.pixelSize: 36
                        font.weight: Font.Bold
                        color: CachyPalette.text
                    }
                    Text {
                        text: "C"
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        color: CachyPalette.textSecondary
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 6
                    }
                }

                Text {
                    text: (root.cardData.condition || i18n("Despejado")).toString().toUpperCase()
                    font.weight: Font.DemiBold
                    font.pixelSize: 11
                    color: CachyPalette.text
                }

                Text {
                    text: root.cardData.isForecast
                        ? i18n("Mín: %1°C · Máx: %2°C", Math.round(Number(root.cardData.tempMin || 0)), Math.round(Number(root.cardData.tempMax || 0)))
                        : i18n("Sensación térmica: %1°C", root.cardData.feelsLike !== undefined ? Math.round(Number(root.cardData.feelsLike)) : "--")
                    font.pixelSize: 10
                    color: CachyPalette.textSecondary
                }
            }

            CachyIcon {
                name: root.cardData.iconName || "weather-few-clouds"
                size: 48
                colored: false
                Layout.alignment: Qt.AlignVCenter
            }
        }

        // 3. Stats Pills (Humedad, Viento, Lluvia)
        RowLayout {
            Layout.fillWidth: true
            spacing: CachyTheme.spacingSmall

            // Humidity
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: stat1.implicitHeight + 8
                radius: CachyTheme.radiusSmall
                color: Qt.rgba(CachyPalette.text.r, CachyPalette.text.g, CachyPalette.text.b, 0.08)
                border.width: 1
                border.color: CachyPalette.specularBorder

                RowLayout {
                    id: stat1
                    anchors.centerIn: parent
                    spacing: 5
                    CachyIcon { name: "weather-fog"; size: 12; color: "#00e5ff" }
                    Text {
                        text: (root.cardData.humidity || "0") + "%"
                        font.pixelSize: 10
                        font.weight: Font.Medium
                        color: CachyPalette.text
                    }
                }
            }

            // Wind
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: stat2.implicitHeight + 8
                radius: CachyTheme.radiusSmall
                color: Qt.rgba(CachyPalette.text.r, CachyPalette.text.g, CachyPalette.text.b, 0.08)
                border.width: 1
                border.color: CachyPalette.specularBorder

                RowLayout {
                    id: stat2
                    anchors.centerIn: parent
                    spacing: 5
                    CachyIcon { name: "weather-windy"; size: 12; color: "#00e5ff" }
                    Text {
                        text: (root.cardData.wind || "0") + " km/h"
                        font.pixelSize: 10
                        font.weight: Font.Medium
                        color: CachyPalette.text
                    }
                }
            }

            // Precipitation
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: stat3.implicitHeight + 8
                radius: CachyTheme.radiusSmall
                color: Qt.rgba(CachyPalette.text.r, CachyPalette.text.g, CachyPalette.text.b, 0.08)
                border.width: 1
                border.color: CachyPalette.specularBorder

                RowLayout {
                    id: stat3
                    anchors.centerIn: parent
                    spacing: 5
                    CachyIcon { name: "weather-showers"; size: 12; color: "#00e5ff" }
                    Text {
                        text: (root.cardData.precipitation || "0") + " mm"
                        font.pixelSize: 10
                        font.weight: Font.Medium
                        color: CachyPalette.text
                    }
                }
            }
        }
    }
}
