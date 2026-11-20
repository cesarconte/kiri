/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import CachyOS.UI

import "cards" as Cards

Item {
    id: convViewRoot

    Layout.fillWidth: true
    Layout.fillHeight: true

    signal openWritingToolsRequested()
    signal openMathNotesRequested()
    signal hideWindowRequested()

    // Empty State (Apple Intelligence suggestions)
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.9, 480)
        visible: kityClient.messages.length === 0
        spacing: 14

        SiriOrbAnimation {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 64
            Layout.preferredHeight: 64
            assistantState: kityClient.state
            audioLevel: kityClient.audioLevel
            customVoiceIndex: kityClient.voiceIndex
        }

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: i18n("¿En qué te puedo ayudar hoy?")
            font.pixelSize: 22
            font.weight: Font.Bold
            color: CachyTheme.textPrimary
        }

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: i18n("Pulsa Meta + K para la píldora flotante rápida o selecciona una sugerencia inteligente:")
            font.pixelSize: 13
            color: CachyTheme.textSecondary
        }

        Flow {
            Layout.fillWidth: true
            Layout.topMargin: 8
            spacing: 8

            Repeater {
                model: [
                    { label: i18n("📋 Resumir portapapeles"), prompt: "Resume lo que he copiado en el portapapeles", isWritingTool: false, isMathNotes: false },
                    { label: i18n("🔔 Resumen de notificaciones"), prompt: "Resume mis notificaciones del sistema", isWritingTool: false, isMathNotes: false },
                    { label: i18n("🖥️ Analizar pantalla"), prompt: "Analiza qué hay en mi pantalla", isWritingTool: false, isMathNotes: false },
                    { label: i18n("⚡ Estado del sistema"), prompt: "Estado del sistema", isWritingTool: false, isMathNotes: false },
                    { label: i18n("✍️ Herramientas de escritura"), prompt: "", isWritingTool: true, isMathNotes: false },
                    { label: i18n("∑ Notas matemáticas"), prompt: "", isWritingTool: false, isMathNotes: true }
                ]

                Rectangle {
                    required property var modelData
                    height: 32
                    implicitWidth: chipText.implicitWidth + 24
                    radius: 16
                    color: chipHover.hovered ? CachyTheme.cardBackgroundHover : CachyTheme.surfacePill
                    border.width: 1
                    border.color: CachyTheme.specularBorder

                    HoverHandler { id: chipHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            if (modelData.isWritingTool) {
                                convViewRoot.openWritingToolsRequested();
                            } else if (modelData.isMathNotes) {
                                convViewRoot.openMathNotesRequested();
                            } else {
                                kityClient.sendMessage(modelData.prompt);
                            }
                        }
                    }

                    Text {
                        id: chipText
                        anchors.centerIn: parent
                        text: modelData.label
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        color: CachyTheme.textPrimary
                    }
                }
            }
        }
    }

    // Message List (Clean Golden Gate Document Presentation - strictly vertical)
    ListView {
        id: chatListView
        anchors.fill: parent
        visible: kityClient.messages.length > 0
        spacing: 16
        clip: true
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        model: kityClient.messages

        QQC2.ScrollBar.vertical: QQC2.ScrollBar {
            policy: QQC2.ScrollBar.AsNeeded
        }

        onCountChanged: Qt.callLater(() => chatListView.positionViewAtEnd())

        Connections {
            target: kityClient
            function onStreamingTextChanged() {
                chatListView.positionViewAtEnd();
            }
        }

        footer: Item {
            width: chatListView.width
            visible: kityClient.streamingText.length > 0
            height: visible ? streamText.implicitHeight + 24 : 0

            TextEdit {
                id: streamText
                width: Math.max(100, parent.width - 18)
                readOnly: true
                selectByMouse: true
                cursorVisible: false
                activeFocusOnPress: false
                wrapMode: TextEdit.Wrap
                color: CachyTheme.textPrimary
                font.pixelSize: Math.round(14 * kityClient.textScale)
                textFormat: TextEdit.RichText
                text: kityClient.renderMarkdown(kityClient.streamingText)
            }
        }

        delegate: Item {
            id: msgDelegate
            required property var modelData
            required property int index

            width: chatListView.width
            implicitHeight: msgCol.implicitHeight + 12

            readonly property bool isUser: modelData.role === "user"
            readonly property var detectedCmds: !isUser
                ? kityClient.extractTerminalCommands(modelData.content)
                : []

            ColumnLayout {
                id: msgCol
                width: Math.max(100, parent.width - 16)
                spacing: 8

                // User Prompt Chip
                Rectangle {
                    visible: msgDelegate.isUser
                    Layout.alignment: Qt.AlignRight
                    Layout.maximumWidth: msgCol.width * 0.82
                    implicitWidth: Math.min(msgCol.width * 0.82, userPromptText.implicitWidth + 24)
                    implicitHeight: userPromptText.implicitHeight + 14
                    radius: 16
                    color: CachyTheme.withAlpha(CachyTheme.accentColor, 0.18)
                    border.width: 1
                    border.color: CachyTheme.withAlpha(CachyTheme.accentColor, 0.45)

                    Text {
                        id: userPromptText
                        anchors.centerIn: parent
                        width: Math.min(implicitWidth, msgCol.width * 0.82 - 24)
                        wrapMode: Text.WordWrap
                        color: CachyTheme.textPrimary
                        font.pixelSize: Math.round(13 * kityClient.textScale)
                        font.weight: Font.Medium
                        text: msgDelegate.modelData.content || ""
                    }
                }

                // Assistant Document-Style Response (09_the_siri_app_window.png)
                TextEdit {
                    visible: !msgDelegate.isUser
                    Layout.fillWidth: true
                    Layout.maximumWidth: msgCol.width
                    readOnly: true
                    selectByMouse: true
                    cursorVisible: false
                    activeFocusOnPress: false
                    wrapMode: TextEdit.Wrap
                    color: CachyTheme.textPrimary
                    font.pixelSize: Math.round(14 * kityClient.textScale)
                    textFormat: TextEdit.RichText
                    onLinkActivated: link => Qt.openUrlExternally(link)
                    text: kityClient.renderMarkdown(msgDelegate.modelData.content || "")
                }

                // Source Citation Pills
                Flow {
                    visible: !msgDelegate.isUser && msgDelegate.modelData.sources !== undefined && msgDelegate.modelData.sources.length > 0
                    Layout.fillWidth: true
                    spacing: 6

                    Repeater {
                        model: msgDelegate.modelData.sources || []

                        Rectangle {
                            required property var modelData
                            height: 22
                            width: srcLabel.implicitWidth + 16
                            radius: 11
                            color: CachyTheme.surfacePill
                            border.width: 1
                            border.color: CachyTheme.specularBorder

                            Text {
                                id: srcLabel
                                anchors.centerIn: parent
                                text: modelData.title || i18n("Source +1")
                                font.pixelSize: 11
                                color: CachyTheme.textSecondary
                            }

                            TapHandler {
                                onTapped: if (modelData.url) Qt.openUrlExternally(modelData.url)
                            }
                        }
                    }
                }

                // Generative Cards
                Cards.WeatherCard {
                    visible: !msgDelegate.isUser && msgDelegate.modelData.cardType === "weather" && msgDelegate.modelData.cardData !== undefined
                    Layout.fillWidth: true
                    cardData: msgDelegate.modelData.cardData || ({})
                }

                Cards.MediaCard {
                    visible: !msgDelegate.isUser && msgDelegate.modelData.cardType === "media" && msgDelegate.modelData.cardData !== undefined
                    Layout.fillWidth: true
                    cardData: msgDelegate.modelData.cardData || ({})
                    onMediaActionTriggered: action => kityClient.controlMedia(action)
                }

                Cards.EventCard {
                    visible: !msgDelegate.isUser && msgDelegate.modelData.cardType === "event" && msgDelegate.modelData.cardData !== undefined
                    Layout.fillWidth: true
                    cardData: msgDelegate.modelData.cardData || ({})
                    onOpenCalendarRequested: kityClient.openKOrganizer()
                }

                Cards.EmailCard {
                    visible: !msgDelegate.isUser && msgDelegate.modelData.cardType === "email" && msgDelegate.modelData.cardData !== undefined
                    Layout.fillWidth: true
                    cardData: msgDelegate.modelData.cardData || ({})
                    onSendRequested: kityClient.sendPendingEmail()
                    onOpenProtonRequested: kityClient.openPendingEmailInProton()
                    onDiscardRequested: kityClient.discardPendingEmail()
                }

                // Writing Tools & Response Action Toolbar (Apple Intelligence Golden Gate)
                Rectangle {
                    visible: !msgDelegate.isUser && (msgDelegate.modelData.content || "").length > 0
                    Layout.fillWidth: true
                    implicitHeight: 34
                    radius: 10
                    color: msgDelegate.modelData.isWritingTool ? Qt.rgba(0.12, 0.14, 0.20, 0.75) : "transparent"
                    border.width: msgDelegate.modelData.isWritingTool ? 1 : 0
                    border.color: CachyTheme.specularBorder

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: msgDelegate.modelData.isWritingTool ? 8 : 0
                        anchors.rightMargin: msgDelegate.modelData.isWritingTool ? 8 : 0
                        spacing: 8

                        // Replace Original Text (Signature Apple Intelligence Action)
                        Rectangle {
                            visible: msgDelegate.modelData.isWritingTool === true
                            height: 26
                            implicitWidth: replaceRow.implicitWidth + 16
                            radius: 13
                            color: replaceHover.hovered ? CachyTheme.withAlpha(CachyTheme.accentColor, 0.35) : CachyTheme.withAlpha(CachyTheme.accentColor, 0.22)
                            border.width: 1
                            border.color: CachyTheme.withAlpha(CachyTheme.accentColor, 0.55)

                            HoverHandler { id: replaceHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                onTapped: {
                                    kityClient.replaceText(msgDelegate.modelData.content);
                                    convViewRoot.hideWindowRequested();
                                }
                            }

                            RowLayout {
                                id: replaceRow
                                anchors.centerIn: parent
                                spacing: 6

                                CachyIcon {
                                    name: "edit-paste-symbolic"
                                    size: 14
                                    color: CachyTheme.accentColor
                                }

                                Text {
                                    text: i18n("Reemplazar original")
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    color: CachyTheme.textPrimary
                                }
                            }
                        }

                        // Copy to clipboard
                        Rectangle {
                            height: 26
                            implicitWidth: copyRow.implicitWidth + 14
                            radius: 13
                            color: copyHover.hovered ? CachyTheme.cardBackgroundHover : CachyTheme.surfacePill
                            border.width: 1
                            border.color: CachyTheme.specularBorder

                            HoverHandler { id: copyHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                onTapped: kityClient.copyToClipboard(msgDelegate.modelData.content)
                            }

                            RowLayout {
                                id: copyRow
                                anchors.centerIn: parent
                                spacing: 5

                                CachyIcon {
                                    name: "edit-copy-symbolic"
                                    size: 13
                                    color: CachyTheme.textSecondary
                                }

                                Text {
                                    text: i18n("Copiar")
                                    font.pixelSize: 11
                                    color: CachyTheme.textPrimary
                                }
                            }
                        }

                        // Speak Aloud
                        Rectangle {
                            height: 26
                            implicitWidth: speakRow.implicitWidth + 14
                            radius: 13
                            color: speakHover.hovered ? CachyTheme.cardBackgroundHover : CachyTheme.surfacePill
                            border.width: 1
                            border.color: CachyTheme.specularBorder

                            HoverHandler { id: speakHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                onTapped: kityClient.speakText(msgDelegate.modelData.content)
                            }

                            RowLayout {
                                id: speakRow
                                anchors.centerIn: parent
                                spacing: 5

                                CachyIcon {
                                    name: "audio-volume-high-symbolic"
                                    size: 13
                                    color: CachyTheme.textSecondary
                                }

                                Text {
                                    text: i18n("Escuchar")
                                    font.pixelSize: 11
                                    color: CachyTheme.textPrimary
                                }
                            }
                        }

                        // Execute in Konsole (when shell commands were detected in response)
                        Rectangle {
                            visible: msgDelegate.detectedCmds.length > 0
                            height: 26
                            implicitWidth: runRow.implicitWidth + 14
                            radius: 13
                            color: runHover.hovered ? CachyTheme.withAlpha("#00e5ff", 0.30) : CachyTheme.withAlpha("#00e5ff", 0.16)
                            border.width: 1
                            border.color: CachyTheme.withAlpha("#00e5ff", 0.45)

                            HoverHandler { id: runHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                onTapped: {
                                    if (msgDelegate.detectedCmds.length > 0) {
                                        kityClient.executeCommandInKonsole(msgDelegate.detectedCmds[0]);
                                    }
                                }
                            }

                            RowLayout {
                                id: runRow
                                anchors.centerIn: parent
                                spacing: 5

                                CachyIcon {
                                    name: "utilities-terminal-symbolic"
                                    size: 13
                                    color: "#00e5ff"
                                }

                                Text {
                                    text: i18n("Ejecutar en Konsole")
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    color: CachyTheme.textPrimary
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }

                        // Quick Tone Switcher (when this was a writing tool)
                        Rectangle {
                            visible: msgDelegate.modelData.isWritingTool === true
                            height: 26
                            implicitWidth: toneRow.implicitWidth + 14
                            radius: 13
                            color: toneHover.hovered ? CachyTheme.cardBackgroundHover : CachyTheme.surfacePill
                            border.width: 1
                            border.color: CachyTheme.specularBorder

                            HoverHandler { id: toneHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                onTapped: toneMenu.popup()
                            }

                            RowLayout {
                                id: toneRow
                                anchors.centerIn: parent
                                spacing: 5

                                CachyIcon {
                                    name: "document-edit-symbolic"
                                    size: 13
                                    color: CachyTheme.textSecondary
                                }

                                Text {
                                    text: i18n("Cambiar tono…")
                                    font.pixelSize: 11
                                    color: CachyTheme.textSecondary
                                }
                            }

                            QQC2.Menu {
                                id: toneMenu
                                QQC2.MenuItem {
                                    text: i18n("💼 Profesional")
                                    onTriggered: kityClient.applyWritingTool("professional", msgDelegate.modelData.sourceText || "")
                                }
                                QQC2.MenuItem {
                                    text: i18n("😊 Amigable")
                                    onTriggered: kityClient.applyWritingTool("friendly", msgDelegate.modelData.sourceText || "")
                                }
                                QQC2.MenuItem {
                                    text: i18n("⚡ Conciso")
                                    onTriggered: kityClient.applyWritingTool("concise", msgDelegate.modelData.sourceText || "")
                                }
                                QQC2.MenuItem {
                                    text: i18n("↻ Reescribir")
                                    onTriggered: kityClient.applyWritingTool("rewrite", msgDelegate.modelData.sourceText || "")
                                }
                                QQC2.MenuItem {
                                    text: i18n("📝 Resumen")
                                    onTriggered: kityClient.applyWritingTool("summary", msgDelegate.modelData.sourceText || "")
                                }
                                QQC2.MenuItem {
                                    text: i18n("• Puntos clave")
                                    onTriggered: kityClient.applyWritingTool("keypoints", msgDelegate.modelData.sourceText || "")
                                }
                                QQC2.MenuItem {
                                    text: i18n("📊 Tabla")
                                    onTriggered: kityClient.applyWritingTool("table", msgDelegate.modelData.sourceText || "")
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
