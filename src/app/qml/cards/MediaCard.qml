/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3

Rectangle {
    id: root
    property var cardData: ({})
    signal mediaActionTriggered(string action)

    implicitHeight: mediaLayout.implicitHeight + Kirigami.Units.gridUnit
    radius: 14
    color: Kirigami.Theme.backgroundColor
    opacity: 0.95
    border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
    border.width: 1

    // Gradient glow from album art or music vibes
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        opacity: 0.2
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#e91e63" }
            GradientStop { position: 1.0; color: "#9c27b0" }
        }
    }

    ColumnLayout {
        id: mediaLayout
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            margins: Kirigami.Units.largeSpacing
        }
        spacing: Kirigami.Units.smallSpacing

        // 1. Header (App Name & Status)
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                source: "audio-card"
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
                color: Kirigami.Theme.highlightColor
            }

            QQC2.Label {
                text: root.cardData.player || i18n("Reproductor multimedia")
                font.weight: Font.DemiBold
                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Rectangle {
                implicitWidth: playStatus.implicitWidth + 12
                implicitHeight: playStatus.implicitHeight + 4
                radius: 8
                color: (root.cardData.status === "Playing" || root.cardData.status === "reproduciendo")
                    ? Qt.rgba(0.0, 0.9, 0.5, 0.2)
                    : Qt.rgba(1.0, 0.7, 0.0, 0.2)
                border.color: (root.cardData.status === "Playing" || root.cardData.status === "reproduciendo")
                    ? "#00e676"
                    : "#ffa000"
                border.width: 1

                QQC2.Label {
                    id: playStatus
                    anchors.centerIn: parent
                    text: (root.cardData.status === "Playing" || root.cardData.status === "reproduciendo")
                        ? i18n("REPRODUCIENDO")
                        : i18n("EN PAUSA")
                    font.pixelSize: Math.max(9, Math.round(Kirigami.Theme.smallFont.pixelSize * 0.9))
                    font.weight: Font.Bold
                    color: (root.cardData.status === "Playing" || root.cardData.status === "reproduciendo")
                        ? "#00e676"
                        : "#ffa000"
                }
            }
        }

        // 2. Track Info & Cover
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.mediumSpacing

            // Album Art Thumbnail or Music Icon
            Rectangle {
                implicitWidth: 48
                implicitHeight: 48
                radius: 10
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
                clip: true

                Image {
                    anchors.fill: parent
                    source: root.cardData.artUrl || ""
                    fillMode: Image.PreserveAspectCrop
                    visible: source !== ""
                }

                Kirigami.Icon {
                    anchors.centerIn: parent
                    source: "media-default-album"
                    implicitWidth: 28
                    implicitHeight: 28
                    visible: !root.cardData.artUrl
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                QQC2.Label {
                    text: root.cardData.title || i18n("Pista de audio")
                    font.weight: Font.Bold
                    font.pixelSize: Kirigami.Theme.defaultFont.pixelSize
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                QQC2.Label {
                    text: {
                        let sub = root.cardData.artist || "";
                        if (root.cardData.album && root.cardData.album !== "") {
                            if (sub !== "") sub += " • ";
                            sub += root.cardData.album;
                        }
                        return sub.length > 0 ? sub : i18n("Audio del sistema");
                    }
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                    color: Kirigami.Theme.disabledTextColor
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }

        // 3. Interactive Playback Controls
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.largeSpacing

            // Previous
            PlasmaComponents3.Button {
                icon.name: "media-skip-backward"
                onClicked: root.mediaActionTriggered("previous")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: i18n("Pista anterior")
            }

            // Play / Pause
            PlasmaComponents3.Button {
                icon.name: (root.cardData.status === "Playing" || root.cardData.status === "reproduciendo")
                    ? "media-playback-pause"
                    : "media-playback-start"
                highlighted: true
                onClicked: root.mediaActionTriggered("play-pause")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: (root.cardData.status === "Playing" || root.cardData.status === "reproduciendo")
                    ? i18n("Pausar")
                    : i18n("Reanudar")
            }

            // Next
            PlasmaComponents3.Button {
                icon.name: "media-skip-forward"
                onClicked: root.mediaActionTriggered("next")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: i18n("Siguiente pista")
            }
        }
    }
}
