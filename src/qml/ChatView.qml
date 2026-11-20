/*
 * SPDX-FileCopyrightText: 2026 Kity contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import CachyOS.UI
import org.kde.plasma.plasmoid

import "cards" as Cards

Rectangle {
    id: root

    signal quickActionTriggered(string prompt, bool isWritingTool)

    function formatTimestamp(raw: string): string {
        if (!raw || raw.length === 0) {
            return "";
        }
        const tIdx = raw.indexOf("T");
        if (tIdx >= 0 && raw.length >= tIdx + 6) {
            return raw.substring(tIdx + 1, tIdx + 6);
        }
        return raw;
    }

    Layout.fillWidth: true
    Layout.fillHeight: true
    radius: CachyTheme.radiusSmall
    color: CachyPalette.cardBackground
    border.width: 1
    border.color: CachyPalette.specularBorder

    // Welcome placeholder when conversation is empty
    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width * 0.92
        visible: Plasmoid.messages.length === 0
        spacing: CachyTheme.spacingLarge

        CachyHeading {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            level: 2
            text: i18n("¿En qué puedo ayudarte?")
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            opacity: 0.75
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            color: CachyPalette.text
            text: i18n("Pulsa la esfera inferior o di «¡Hola Kity!» para hablar, o elige una acción rápida:")
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: CachyTheme.spacingSmall
            rowSpacing: CachyTheme.spacingSmall
            uniformCellWidths: true

            Repeater {
                model: [
                    { label: i18n("Analizar pantalla"), icon: "camera-photo-symbolic", prompt: "Analiza qué hay en mi pantalla", isWritingTool: false },
                    { label: i18n("Herramientas de escritura"), icon: "document-edit-symbolic", prompt: "", isWritingTool: true },
                    { label: i18n("Resumir copiado"), icon: "edit-copy-symbolic", prompt: "Resume lo que he copiado en el portapapeles", isWritingTool: false },
                    { label: i18n("Notificaciones"), icon: "preferences-desktop-notification-bell", prompt: "Resume mis notificaciones del sistema", isWritingTool: false },
                    { label: i18n("Estado del sistema"), icon: "utilities-system-monitor", prompt: "Estado del sistema", isWritingTool: false },
                    { label: i18n("¿Qué canción suena?"), icon: "emblem-music-symbolic", prompt: "¿Qué canción está sonando?", isWritingTool: false },
                    { label: i18n("Modo oscuro"), icon: "weather-clear-night", prompt: "Activa el modo oscuro", isWritingTool: false }
                ]

                CachyButton {
                    required property var modelData
                    Layout.fillWidth: true
                    iconName: modelData.icon
                    text: modelData.label
                    font.pixelSize: 11
                    onClicked: root.quickActionTriggered(modelData.prompt, modelData.isWritingTool)
                }
            }
        }
    }

    QQC2.ScrollView {
        anchors.fill: parent
        anchors.margins: CachyTheme.spacingSmall
        visible: Plasmoid.messages.length > 0
        clip: true

        ListView {
            id: chatListView
            anchors.fill: parent
            spacing: CachyTheme.spacingMedium
            model: Plasmoid.messages

            onCountChanged: {
                Qt.callLater(() => chatListView.positionViewAtEnd());
            }

            Connections {
                target: Plasmoid
                function onStreamingTextChanged() {
                    chatListView.positionViewAtEnd();
                }
            }

            footer: Item {
                width: chatListView.width
                visible: Plasmoid.streamingText.length > 0
                height: visible ? streamBubble.height + CachyTheme.spacingMedium : 0

                Rectangle {
                    id: streamBubble
                    width: parent.width * 0.92
                    height: streamText.implicitHeight + CachyTheme.spacingMedium * 2
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.topMargin: CachyTheme.spacingSmall
                    radius: CachyTheme.radiusSmall
                    color: CachyPalette.isDark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.05)
                    border.width: 1
                    border.color: CachyPalette.specularBorder

                    TextEdit {
                        id: streamText
                        x: CachyTheme.spacingMedium
                        y: CachyTheme.spacingMedium
                        width: streamBubble.width - CachyTheme.spacingMedium * 2
                        readOnly: true
                        selectByMouse: true
                        cursorVisible: false
                        activeFocusOnPress: false
                        wrapMode: TextEdit.Wrap
                        color: CachyPalette.text
                        font.pixelSize: 13
                        textFormat: TextEdit.RichText
                        text: Plasmoid.renderMarkdown(Plasmoid.streamingText)
                    }
                }
            }

            delegate: Item {
                id: msgDelegate
                required property var modelData
                required property int index

                width: chatListView.width
                implicitHeight: bubbleRect.height

                readonly property bool isUser: modelData.role === "user"
                readonly property var detectedCmds: !isUser
                    ? Plasmoid.extractTerminalCommands(modelData.content)
                    : []

                Rectangle {
                    id: bubbleRect
                    width: msgDelegate.isUser
                        ? Math.min(parent.width * 0.88, Math.max(96, msgColumn.implicitWidth + CachyTheme.spacingMedium * 2))
                        : parent.width * 0.92
                    height: msgColumn.implicitHeight + CachyTheme.spacingMedium * 2
                    anchors.right: msgDelegate.isUser ? parent.right : undefined
                    anchors.left: msgDelegate.isUser ? undefined : parent.left
                    radius: CachyTheme.radiusSmall
                    color: msgDelegate.isUser
                        ? Qt.rgba(CachyTheme.accentColor.r, CachyTheme.accentColor.g, CachyTheme.accentColor.b, 0.22)
                        : (CachyPalette.isDark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.05))
                    border.width: 1
                    border.color: msgDelegate.isUser
                        ? CachyTheme.accentColor
                        : CachyPalette.specularBorder

                    ColumnLayout {
                        id: msgColumn
                        x: CachyTheme.spacingMedium
                        y: CachyTheme.spacingMedium
                        width: bubbleRect.width - CachyTheme.spacingMedium * 2
                        spacing: CachyTheme.spacingSmall

                        TextEdit {
                            id: msgText
                            Layout.fillWidth: true
                            readOnly: true
                            selectByMouse: true
                            cursorVisible: false
                            activeFocusOnPress: false
                            wrapMode: TextEdit.Wrap
                            color: CachyPalette.text
                            font.pixelSize: 13
                            textFormat: msgDelegate.isUser ? TextEdit.PlainText : TextEdit.RichText
                            onLinkActivated: link => Qt.openUrlExternally(link)
                            text: msgDelegate.isUser
                                ? msgDelegate.modelData.content
                                : Plasmoid.renderMarkdown(msgDelegate.modelData.content)

                            HoverHandler {
                                cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.IBeamCursor
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: CachyTheme.spacingSmall
                            visible: msgDelegate.detectedCmds.length > 0

                            Repeater {
                                model: msgDelegate.detectedCmds

                                Rectangle {
                                    id: cmdCard
                                    required property string modelData
                                    Layout.fillWidth: true
                                    implicitHeight: cmdRow.implicitHeight + CachyTheme.spacingSmall * 2
                                    color: CachyPalette.isDark ? Qt.rgba(0, 0, 0, 0.35) : Qt.rgba(0, 0, 0, 0.06)
                                    radius: CachyTheme.radiusSmall
                                    border.width: 1
                                    border.color: CachyTheme.withAlpha("#00e5ff", 0.40)

                                    RowLayout {
                                        id: cmdRow
                                        anchors.fill: parent
                                        anchors.margins: CachyTheme.spacingSmall
                                        spacing: CachyTheme.spacingSmall

                                        CachyIcon {
                                            name: "utilities-terminal-symbolic"
                                            size: 14
                                            color: "#00e5ff"
                                        }

                                        TextEdit {
                                            Layout.fillWidth: true
                                            readOnly: true
                                            selectByMouse: true
                                            cursorVisible: false
                                            activeFocusOnPress: false
                                            wrapMode: TextEdit.Wrap
                                            font.family: "monospace"
                                            font.pixelSize: 11
                                            color: CachyPalette.text
                                            text: cmdCard.modelData
                                        }

                                        CachyButton {
                                            iconName: "media-playback-start-symbolic"
                                            text: i18n("Ejecutar en Konsole")
                                            font.pixelSize: 11
                                            onClicked: {
                                                Plasmoid.executeCommandInKonsole(cmdCard.modelData);
                                            }
                                        }

                                        CachyToolbarButton {
                                            iconName: "edit-copy-symbolic"
                                            implicitWidth: 26
                                            implicitHeight: 26
                                            tooltipText: i18n("Copiar orden al portapapeles")
                                            onClicked: {
                                                Plasmoid.copyToClipboard(cmdCard.modelData);
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Generative Interactive Cards
                        Cards.WeatherCard {
                            visible: !msgDelegate.isUser && msgDelegate.modelData.cardType === "weather" && msgDelegate.modelData.cardData !== undefined
                            Layout.fillWidth: true
                            cardData: msgDelegate.modelData.cardData || ({})
                        }

                        Cards.MediaCard {
                            visible: !msgDelegate.isUser && msgDelegate.modelData.cardType === "media" && msgDelegate.modelData.cardData !== undefined
                            Layout.fillWidth: true
                            cardData: msgDelegate.modelData.cardData || ({})
                            onMediaActionTriggered: action => Plasmoid.controlMedia(action)
                        }

                        Cards.EventCard {
                            visible: !msgDelegate.isUser && msgDelegate.modelData.cardType === "event" && msgDelegate.modelData.cardData !== undefined
                            Layout.fillWidth: true
                            cardData: msgDelegate.modelData.cardData || ({})
                            onOpenCalendarRequested: Plasmoid.openKOrganizer()
                        }

                        Cards.EmailCard {
                            visible: !msgDelegate.isUser && msgDelegate.modelData.cardType === "email" && msgDelegate.modelData.cardData !== undefined
                            Layout.fillWidth: true
                            cardData: msgDelegate.modelData.cardData || ({})
                            onSendRequested: Plasmoid.sendPendingEmail()
                            onOpenProtonRequested: Plasmoid.openPendingEmailInProton()
                            onDiscardRequested: Plasmoid.discardPendingEmail()
                        }

                        // Interactive Source Chips
                        Flow {
                            visible: !msgDelegate.isUser && msgDelegate.modelData.sources !== undefined && msgDelegate.modelData.sources.length > 0
                            Layout.fillWidth: true
                            spacing: CachyTheme.spacingSmall

                            Repeater {
                                model: msgDelegate.modelData.sources || []

                                CachyButton {
                                    required property var modelData
                                    iconName: "globe"
                                    text: modelData.title || i18n("Fuente")
                                    font.pixelSize: 11
                                    onClicked: Qt.openUrlExternally(modelData.url)
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: CachyTheme.spacingSmall

                            Text {
                                text: root.formatTimestamp(msgDelegate.modelData.timestamp ?? "")
                                font.pixelSize: 10
                                color: CachyPalette.textSecondary
                                opacity: 0.65
                            }

                            Item { Layout.fillWidth: true }

                            // Replace Original Text (Writing Tools Apple Intelligence action)
                            CachyButton {
                                visible: !msgDelegate.isUser && msgDelegate.modelData.isWritingTool === true
                                iconName: "edit-paste-symbolic"
                                text: i18n("Reemplazar original")
                                font.pixelSize: 11
                                onClicked: {
                                    Plasmoid.replaceText(msgDelegate.modelData.content);
                                    Plasmoid.expanded = false;
                                }
                            }

                            // Speak / Stop button on Assistant responses
                            CachyToolbarButton {
                                visible: !msgDelegate.isUser && msgDelegate.modelData.content.length > 0
                                iconName: Plasmoid.isSpeaking ? "media-playback-stop-symbolic" : "audio-volume-high-symbolic"
                                implicitWidth: 26
                                implicitHeight: 26
                                tooltipText: Plasmoid.isSpeaking ? i18n("Detener lectura") : i18n("Leer en voz alta")
                                onClicked: {
                                    if (Plasmoid.isSpeaking) {
                                        Plasmoid.stopSpeaking();
                                    } else {
                                        Plasmoid.speakText(msgDelegate.modelData.content);
                                    }
                                }
                            }

                            // Copy message button
                            CachyToolbarButton {
                                visible: msgDelegate.modelData.content.length > 0
                                iconName: "edit-copy-symbolic"
                                implicitWidth: 26
                                implicitHeight: 26
                                tooltipText: i18n("Copiar texto")
                                onClicked: {
                                    Plasmoid.copyToClipboard(msgDelegate.modelData.content);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
