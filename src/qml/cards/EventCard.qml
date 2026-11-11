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
    signal openCalendarRequested()

    implicitHeight: eventLayout.implicitHeight + Kirigami.Units.gridUnit
    radius: 14
    color: Kirigami.Theme.backgroundColor
    opacity: 0.95
    border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
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
            margins: Kirigami.Units.largeSpacing
        }
        spacing: Kirigami.Units.smallSpacing

        // 1. Header (Calendar Source & Badge)
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                source: "view-calendar"
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
                color: "#ff1744"
            }

            QQC2.Label {
                text: root.cardData.calendarName || i18n("Google Calendar (KOrganizer)")
                font.weight: Font.DemiBold
                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize
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

                QQC2.Label {
                    id: calBadge
                    anchors.centerIn: parent
                    text: i18n("AGENDADO")
                    font.pixelSize: Math.max(9, Math.round(Kirigami.Theme.smallFont.pixelSize * 0.9))
                    font.weight: Font.Bold
                    color: "#ff1744"
                }
            }
        }

        // 2. Main Event Content with Date Badge
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            // Date Badge (iOS/macOS/KOrganizer Calendar Pad Style)
            Rectangle {
                implicitWidth: 54
                implicitHeight: 60
                radius: 10
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)
                border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
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

                    QQC2.Label {
                        anchors.centerIn: parent
                        text: (root.cardData.monthShort || "NOV").toString().toUpperCase()
                        font.pixelSize: 10
                        font.weight: Font.Bold
                        color: "#ffffff"
                    }
                }

                // Day number
                QQC2.Label {
                    anchors {
                        bottom: parent.bottom
                        horizontalCenter: parent.horizontalCenter
                        bottomMargin: 4
                    }
                    text: root.cardData.dayNumber || "1"
                    font.pixelSize: 22
                    font.weight: Font.Bold
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                QQC2.Label {
                    text: root.cardData.title || i18n("Cita agendada")
                    font.weight: Font.Bold
                    font.pixelSize: Kirigami.Theme.defaultFont.pixelSize
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                RowLayout {
                    spacing: 4
                    Kirigami.Icon {
                        source: "preferences-system-time"
                        implicitWidth: 14
                        implicitHeight: 14
                        color: Kirigami.Theme.disabledTextColor
                    }
                    QQC2.Label {
                        text: {
                            let s = root.cardData.dateFormatted || "";
                            if (root.cardData.startTime) {
                                if (s.length > 0) s += " • ";
                                s += root.cardData.startTime;
                                if (root.cardData.endTime) s += " – " + root.cardData.endTime;
                            }
                            return s.length > 0 ? s : i18n("Hora no especificada");
                        }
                        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                        color: Kirigami.Theme.disabledTextColor
                    }
                }

                QQC2.Label {
                    text: root.cardData.description || ""
                    font.pixelSize: Math.max(9, Math.round(Kirigami.Theme.smallFont.pixelSize * 0.9))
                    color: Kirigami.Theme.disabledTextColor
                    visible: text.length > 0
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }

        // 3. Action Footer
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing

            Item { Layout.fillWidth: true }

            QQC2.Button {
                icon.name: "korganizer"
                text: i18n("Abrir en KOrganizer")
                onClicked: root.openCalendarRequested()
            }
        }
    }
}
