/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
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
    radius: Kirigami.Units.smallSpacing
    color: Kirigami.Theme.alternateBackgroundColor
    border.width: 1
    border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.14)

    // Welcome placeholder when conversation is empty
    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width * 0.92
        visible: Plasmoid.messages.length === 0
        spacing: Kirigami.Units.largeSpacing

        Kirigami.Heading {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            level: 2
            text: i18n("¿En qué puedo ayudarte?")
            wrapMode: Text.WordWrap
        }

        QQC2.Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            opacity: 0.75
            wrapMode: Text.WordWrap
            text: i18n("Pulsa la esfera inferior o di «¡Hola Kity!» para hablar, o elige una acción rápida:")
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Kirigami.Units.smallSpacing
            rowSpacing: Kirigami.Units.smallSpacing
            uniformCellWidths: true

            Repeater {
                model: [
                    { label: "🖥️ Analizar pantalla", prompt: "Analiza qué hay en mi pantalla", isWritingTool: false },
                    { label: "✍️ Herramientas de escritura", prompt: "", isWritingTool: true },
                    { label: "📋 Resumir copiado", prompt: "Resume lo que he copiado en el portapapeles", isWritingTool: false },
                    { label: "🔔 Notificaciones", prompt: "Resume mis notificaciones del sistema", isWritingTool: false },
                    { label: "⚡ Estado del sistema", prompt: "Estado del sistema", isWritingTool: false },
                    { label: "🎵 ¿Qué canción suena?", prompt: "¿Qué canción está sonando?", isWritingTool: false },
                    { label: "🌙 Modo oscuro", prompt: "Activa el modo oscuro", isWritingTool: false }
                ]

                QQC2.Button {
                    id: expActionBtn
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.minimumHeight: Kirigami.Units.gridUnit * 2.3
                    text: modelData.label
                    font.pixelSize: Kirigami.Theme.defaultFont.pixelSize
                    hoverEnabled: true

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: expActionBtn.text
                            color: expActionBtn.down ? palette.highlight : palette.buttonText
                            font: expActionBtn.font
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    background: Rectangle {
                        radius: 10
                        color: expActionBtn.down
                            ? Kirigami.ColorUtils.tintWithAlpha(palette.button, palette.highlight, 0.25)
                            : (expActionBtn.hovered
                                ? Kirigami.ColorUtils.tintWithAlpha(palette.button, palette.highlight, 0.12)
                                : palette.button)
                        border.width: 1
                        border.color: expActionBtn.hovered
                            ? palette.highlight
                            : Kirigami.ColorUtils.linearInterpolation(palette.window, palette.windowText, 0.15)

                        Behavior on color { ColorAnimation { duration: 150 } }
                        Behavior on border.color { ColorAnimation { duration: 150 } }
                    }

                    onClicked: root.quickActionTriggered(modelData.prompt, modelData.isWritingTool)
                }
            }
        }
    }

    QQC2.ScrollView {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing
        visible: Plasmoid.messages.length > 0
        clip: true

        ListView {
            id: chatListView
            anchors.fill: parent
            spacing: Kirigami.Units.mediumSpacing
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
                height: visible ? streamBubble.height + Kirigami.Units.mediumSpacing : 0

                Rectangle {
                    id: streamBubble
                    width: parent.width * 0.92
                    height: streamText.implicitHeight + Kirigami.Units.mediumSpacing * 2
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.topMargin: Kirigami.Units.smallSpacing
                    radius: Kirigami.Units.smallSpacing
                    color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.alternateBackgroundColor, 0.6)
                    border.width: 1
                    border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.14)

                    TextEdit {
                        id: streamText
                        x: Kirigami.Units.mediumSpacing
                        y: Kirigami.Units.mediumSpacing
                        width: streamBubble.width - Kirigami.Units.mediumSpacing * 2
                        readOnly: true
                        selectByMouse: true
                        cursorVisible: false
                        activeFocusOnPress: false
                        wrapMode: TextEdit.Wrap
                        color: Kirigami.Theme.textColor
                        font: Kirigami.Theme.defaultFont
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
                        ? Math.min(parent.width * 0.88, Math.max(Kirigami.Units.gridUnit * 6, msgColumn.implicitWidth + Kirigami.Units.mediumSpacing * 2))
                        : parent.width * 0.92
                    height: msgColumn.implicitHeight + Kirigami.Units.mediumSpacing * 2
                    anchors.right: msgDelegate.isUser ? parent.right : undefined
                    anchors.left: msgDelegate.isUser ? undefined : parent.left
                    radius: Kirigami.Units.smallSpacing
                    color: msgDelegate.isUser
                        ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.18)
                        : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.alternateBackgroundColor, 0.6)
                    border.width: 1
                    border.color: msgDelegate.isUser
                        ? Kirigami.Theme.highlightColor
                        : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.14)

                    ColumnLayout {
                        id: msgColumn
                        x: Kirigami.Units.mediumSpacing
                        y: Kirigami.Units.mediumSpacing
                        width: bubbleRect.width - Kirigami.Units.mediumSpacing * 2
                        spacing: Kirigami.Units.smallSpacing

                        TextEdit {
                            id: msgText
                            Layout.fillWidth: true
                            readOnly: true
                            selectByMouse: true
                            cursorVisible: false
                            activeFocusOnPress: false
                            wrapMode: TextEdit.Wrap
                            color: Kirigami.Theme.textColor
                            font: Kirigami.Theme.defaultFont
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
                            spacing: Kirigami.Units.smallSpacing
                            visible: msgDelegate.detectedCmds.length > 0

                            Repeater {
                                model: msgDelegate.detectedCmds

                                Rectangle {
                                    id: cmdCard
                                    required property string modelData
                                    Layout.fillWidth: true
                                    implicitHeight: cmdRow.implicitHeight + Kirigami.Units.smallSpacing * 2
                                    color: Kirigami.Theme.alternateBackgroundColor
                                    radius: Kirigami.Units.smallSpacing
                                    border.width: 1
                                    border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.highlightColor, 0.5)

                                    RowLayout {
                                        id: cmdRow
                                        anchors.fill: parent
                                        anchors.margins: Kirigami.Units.smallSpacing
                                        spacing: Kirigami.Units.smallSpacing

                                        Kirigami.Icon {
                                            source: "utilities-terminal-symbolic"
                                            implicitWidth: Kirigami.Units.iconSizes.small
                                            implicitHeight: Kirigami.Units.iconSizes.small
                                            color: Kirigami.Theme.highlightColor
                                        }

                                        TextEdit {
                                            Layout.fillWidth: true
                                            readOnly: true
                                            selectByMouse: true
                                            cursorVisible: false
                                            activeFocusOnPress: false
                                            wrapMode: TextEdit.Wrap
                                            font.family: "monospace"
                                            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                                            color: Kirigami.Theme.textColor
                                            text: cmdCard.modelData
                                        }

                                        QQC2.Button {
                                            icon.name: "media-playback-start-symbolic"
                                            text: i18n("Ejecutar en Konsole")
                                            onClicked: {
                                                Plasmoid.executeCommandInKonsole(cmdCard.modelData);
                                            }
                                            QQC2.ToolTip.visible: hovered
                                            QQC2.ToolTip.text: i18n("Ejecutar orden en una ventana interactiva de Konsole")
                                        }

                                        QQC2.ToolButton {
                                            icon.name: "edit-copy-symbolic"
                                            flat: true
                                            implicitWidth: Kirigami.Units.iconSizes.smallMedium
                                            implicitHeight: Kirigami.Units.iconSizes.smallMedium
                                            onClicked: {
                                                Plasmoid.copyToClipboard(cmdCard.modelData);
                                            }
                                            QQC2.ToolTip.visible: hovered
                                            QQC2.ToolTip.text: i18n("Copiar orden al portapapeles")
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
                            spacing: Kirigami.Units.smallSpacing

                            Repeater {
                                model: msgDelegate.modelData.sources || []

                                QQC2.Button {
                                    required property var modelData
                                    icon.name: "globe"
                                    text: modelData.title || i18n("Fuente")
                                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                                    onClicked: Qt.openUrlExternally(modelData.url)
                                    QQC2.ToolTip.visible: hovered
                                    QQC2.ToolTip.text: modelData.url || ""
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.Label {
                                text: root.formatTimestamp(msgDelegate.modelData.timestamp ?? "")
                                font: Kirigami.Theme.smallFont
                                opacity: 0.55
                            }

                            Item { Layout.fillWidth: true }

                            // Replace Original Text (Writing Tools Apple Intelligence action)
                            QQC2.Button {
                                visible: !msgDelegate.isUser && msgDelegate.modelData.isWritingTool === true
                                icon.name: "edit-paste-symbolic"
                                text: i18n("Reemplazar original")
                                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                                onClicked: {
                                    Plasmoid.replaceText(msgDelegate.modelData.content);
                                    Plasmoid.expanded = false;
                                }
                                QQC2.ToolTip.visible: hovered
                                QQC2.ToolTip.text: i18n("Copia el texto al portapapeles y cierra Kity para pegar (Ctrl+V) en tu aplicación")
                            }

                            // Speak / Stop button on Assistant responses
                            QQC2.ToolButton {
                                visible: !msgDelegate.isUser && msgDelegate.modelData.content.length > 0
                                icon.name: Plasmoid.isSpeaking ? "media-playback-stop-symbolic" : "audio-volume-high-symbolic"
                                flat: true
                                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                                implicitHeight: Kirigami.Units.iconSizes.smallMedium
                                onClicked: {
                                    if (Plasmoid.isSpeaking) {
                                        Plasmoid.stopSpeaking();
                                    } else {
                                        Plasmoid.speakText(msgDelegate.modelData.content);
                                    }
                                }
                                QQC2.ToolTip.visible: hovered
                                QQC2.ToolTip.text: Plasmoid.isSpeaking ? i18n("Detener lectura") : i18n("Leer en voz alta")
                            }

                            // Copy message button
                            QQC2.ToolButton {
                                visible: msgDelegate.modelData.content.length > 0
                                icon.name: "edit-copy-symbolic"
                                flat: true
                                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                                implicitHeight: Kirigami.Units.iconSizes.smallMedium
                                onClicked: {
                                    Plasmoid.copyToClipboard(msgDelegate.modelData.content);
                                }
                                QQC2.ToolTip.visible: hovered
                                QQC2.ToolTip.text: i18n("Copiar texto")
                            }
                        }
                    }
                }
            }
        }
    }
}
