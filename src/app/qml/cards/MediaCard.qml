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
    signal mediaActionTriggered(string action)

    implicitHeight: mediaLayout.implicitHeight + 16
    radius: CachyTheme.radiusMedium
    color: Qt.rgba(0.08, 0.10, 0.14, 0.88)
    border.color: CachyPalette.specularBorder
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
            margins: CachyTheme.spacingMedium
        }
        spacing: CachyTheme.spacingSmall

        // 1. Header (App Name & Status)
        RowLayout {
            Layout.fillWidth: true
            spacing: CachyTheme.spacingSmall

            CachyIcon {
                name: "audio-card"
                size: 16
                color: CachyTheme.accentColor
            }

            Text {
                text: root.cardData.player || i18n("Reproductor multimedia")
                font.weight: Font.DemiBold
                font.pixelSize: 13
                color: CachyPalette.text
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

                Text {
                    id: playStatus
                    anchors.centerIn: parent
                    text: (root.cardData.status === "Playing" || root.cardData.status === "reproduciendo")
                        ? i18n("REPRODUCIENDO")
                        : i18n("EN PAUSA")
                    font.pixelSize: 10
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
            spacing: CachyTheme.spacingMedium

            // Album Art Thumbnail or Music Icon
            Rectangle {
                implicitWidth: 48
                implicitHeight: 48
                radius: 10
                color: Qt.rgba(CachyPalette.text.r, CachyPalette.text.g, CachyPalette.text.b, 0.1)
                border.width: 1
                border.color: CachyPalette.specularBorder
                clip: true

                Image {
                    anchors.fill: parent
                    source: root.cardData.artUrl || ""
                    fillMode: Image.PreserveAspectCrop
                    visible: source !== ""
                }

                CachyIcon {
                    anchors.centerIn: parent
                    name: "media-default-album"
                    size: 28
                    visible: !root.cardData.artUrl
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: root.cardData.title || i18n("Pista de audio")
                    font.weight: Font.Bold
                    font.pixelSize: 13
                    color: CachyPalette.text
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    text: {
                        let sub = root.cardData.artist || "";
                        if (root.cardData.album && root.cardData.album !== "") {
                            if (sub !== "") sub += " • ";
                            sub += root.cardData.album;
                        }
                        return sub.length > 0 ? sub : i18n("Audio del sistema");
                    }
                    font.pixelSize: 11
                    color: CachyPalette.textSecondary
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }

        // 3. Interactive Playback Controls
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: CachyTheme.spacingSmall
            spacing: CachyTheme.spacingLarge

            CachyToolbarButton {
                iconName: "media-skip-backward-symbolic"
                tooltipText: i18n("Pista anterior")
                onClicked: root.mediaActionTriggered("previous")
            }

            CachyToolbarButton {
                iconName: (root.cardData.status === "Playing" || root.cardData.status === "reproduciendo")
                    ? "media-playback-pause-symbolic"
                    : "media-playback-start-symbolic"
                primary: true
                tooltipText: (root.cardData.status === "Playing" || root.cardData.status === "reproduciendo")
                    ? i18n("Pausar")
                    : i18n("Reproducir")
                onClicked: root.mediaActionTriggered("play_pause")
            }

            CachyToolbarButton {
                iconName: "media-skip-forward-symbolic"
                tooltipText: i18n("Siguiente pista")
                onClicked: root.mediaActionTriggered("next")
            }
        }
    }
}
