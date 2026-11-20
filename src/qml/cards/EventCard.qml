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
    signal openCalendarRequested()

    implicitHeight: eventLayout.implicitHeight + 16
    radius: CachyTheme.radiusMedium
    color: Qt.rgba(0.08, 0.10, 0.14, 0.88)
    border.color: CachyPalette.specularBorder
    border.width: 1

    // Subtle calendar red/crimson accent tint
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        opacity: 0.15
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#ff1744" }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    ColumnLayout {
        id: eventLayout
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            margins: CachyTheme.spacingMedium
        }
        spacing: CachyTheme.spacingSmall

        // 1. Header (Calendar Source & Badge)
        RowLayout {
            Layout.fillWidth: true
            spacing: CachyTheme.spacingSmall

            CachyIcon {
                name: "view-calendar"
                size: 16
                color: "#ff1744"
            }

            Text {
                text: root.cardData.calendarName || i18n("Google Calendar (KOrganizer)")
                font.weight: Font.DemiBold
                font.pixelSize: 13
                color: CachyPalette.text
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Rectangle {
                implicitWidth: calBadge.implicitWidth + 12
                implicitHeight: calBadge.implicitHeight + 4
                radius: 8
                color: Qt.rgba(1.0, 0.1, 0.25, 0.2)
                border.color: "#ff1744"
                border.width: 1

                Text {
                    id: calBadge
                    anchors.centerIn: parent
                    text: i18n("AGENDADO")
                    font.pixelSize: 10
                    font.weight: Font.Bold
                    color: "#ff1744"
                }
            }
        }

        // 2. Main Event Content with Date Badge
        RowLayout {
            Layout.fillWidth: true
            spacing: CachyTheme.spacingMedium

            // Date Badge (Calendar Pad Style)
            Rectangle {
                implicitWidth: 54
                implicitHeight: 60
                radius: 10
                color: Qt.rgba(CachyPalette.text.r, CachyPalette.text.g, CachyPalette.text.b, 0.08)
                border.color: CachyPalette.specularBorder
                border.width: 1
                clip: true

                // Header band
                Rectangle {
                    anchors {
                        top: parent.top
                        left: parent.left
                        right: parent.right
                    }
                    height: 18
                    color: "#ff1744"

                    Text {
                        anchors.centerIn: parent
                        text: (root.cardData.monthShort || "NOV").toString().toUpperCase()
                        font.pixelSize: 10
                        font.weight: Font.Bold
                        color: "#ffffff"
                    }
                }

                // Day number
                Text {
                    anchors {
                        bottom: parent.bottom
                        horizontalCenter: parent.horizontalCenter
                        bottomMargin: 4
                    }
                    text: root.cardData.dayNumber || "1"
                    font.pixelSize: 22
                    font.weight: Font.Bold
                    color: CachyPalette.text
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: root.cardData.title || i18n("Cita agendada")
                    font.weight: Font.Bold
                    font.pixelSize: 13
                    color: CachyPalette.text
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                RowLayout {
                    spacing: 4
                    CachyIcon {
                        name: "time"
                        size: 12
                        color: CachyPalette.textSecondary
                    }
                    Text {
                        text: {
                            let tm = root.cardData.timeStart || "";
                            if (root.cardData.timeEnd && root.cardData.timeEnd !== "") {
                                if (tm !== "") tm += " – ";
                                tm += root.cardData.timeEnd;
                            }
                            return tm.length > 0 ? tm : i18n("Todo el día");
                        }
                        font.pixelSize: 11
                        color: CachyPalette.textSecondary
                    }
                }

                Text {
                    visible: root.cardData.location !== undefined && root.cardData.location.length > 0
                    text: root.cardData.location || ""
                    font.pixelSize: 10
                    color: CachyPalette.textSecondary
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }

        // 3. Action
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: CachyTheme.spacingSmall

            Item { Layout.fillWidth: true }

            CachyButton {
                iconName: "view-calendar"
                text: i18n("Abrir en KOrganizer")
                onClicked: root.openCalendarRequested()
            }
        }
    }
}
