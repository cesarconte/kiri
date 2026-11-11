/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Rectangle {
    id: root
    property var cardData: ({})

    implicitHeight: mainLayout.implicitHeight + Kirigami.Units.gridUnit
    radius: 14
    color: Kirigami.Theme.backgroundColor
    opacity: 0.95
    border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
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
            margins: Kirigami.Units.largeSpacing
        }
        spacing: Kirigami.Units.smallSpacing

        // 1. Header (Location & Badge)
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                source: "mark-location"
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
                color: Kirigami.Theme.highlightColor
            }

            QQC2.Label {
                text: root.cardData.location || root.cardData.city || i18n("Tiempo actual")
                font.weight: Font.DemiBold
                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize
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

                QQC2.Label {
                    id: liveLabel
                    anchors.centerIn: parent
                    text: i18n("EN VIVO")
                    font.pixelSize: Math.max(9, Math.round(Kirigami.Theme.smallFont.pixelSize * 0.9))
                    font.weight: Font.Bold
                    color: "#00e676"
                }
            }
        }

        // 2. Main Temperature & Weather Icon
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.largeSpacing

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                RowLayout {
                    spacing: 4
                    QQC2.Label {
                        text: (root.cardData.temp !== undefined ? Math.round(Number(root.cardData.temp)) : "--") + "°"
                        font.pixelSize: 36
                        font.weight: Font.Bold
                    }
                    QQC2.Label {
                        text: "C"
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        color: Kirigami.Theme.disabledTextColor
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 6
                    }
                }

                QQC2.Label {
                    text: (root.cardData.condition || i18n("Despejado")).toString().toUpperCase()
                    font.weight: Font.DemiBold
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                    color: Kirigami.Theme.textColor
                }

                QQC2.Label {
                    text: i18n("Sensación térmica: %1°C", root.cardData.feelsLike !== undefined ? Math.round(Number(root.cardData.feelsLike)) : "--")
                    font.pixelSize: Math.max(9, Math.round(Kirigami.Theme.smallFont.pixelSize * 0.9))
                    color: Kirigami.Theme.disabledTextColor
                }
            }

            Kirigami.Icon {
                source: root.cardData.iconName || "weather-few-clouds"
                implicitWidth: 48
                implicitHeight: 48
                Layout.alignment: Qt.AlignVCenter
            }
        }

        // 3. Stats Pills (Humedad, Viento, Lluvia)
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            // Humidity
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: stat1.implicitHeight + 8
                radius: 8
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)

                RowLayout {
                    id: stat1
                    anchors.centerIn: parent
                    spacing: 4
                    QQC2.Label { text: "💧"; font.pixelSize: 11 }
                    QQC2.Label {
                        text: (root.cardData.humidity || "0") + "%"
                        font.pixelSize: Math.max(9, Math.round(Kirigami.Theme.smallFont.pixelSize * 0.9))
                        font.weight: Font.Medium
                    }
                }
            }

            // Wind
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: stat2.implicitHeight + 8
                radius: 8
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)

                RowLayout {
                    id: stat2
                    anchors.centerIn: parent
                    spacing: 4
                    QQC2.Label { text: "💨"; font.pixelSize: 11 }
                    QQC2.Label {
                        text: (root.cardData.wind || "0") + " km/h"
                        font.pixelSize: Math.max(9, Math.round(Kirigami.Theme.smallFont.pixelSize * 0.9))
                        font.weight: Font.Medium
                    }
                }
            }

            // Precipitation
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: stat3.implicitHeight + 8
                radius: 8
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)

                RowLayout {
                    id: stat3
                    anchors.centerIn: parent
                    spacing: 4
                    QQC2.Label { text: "🌧️"; font.pixelSize: 11 }
                    QQC2.Label {
                        text: (root.cardData.precipitation || "0") + " mm"
                        font.pixelSize: Math.max(9, Math.round(Kirigami.Theme.smallFont.pixelSize * 0.9))
                        font.weight: Font.Medium
                    }
                }
            }
        }
    }
}
