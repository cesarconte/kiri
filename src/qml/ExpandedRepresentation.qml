/*
    SPDX-FileCopyrightText: 2026 Kiri contributors

    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.plasmoid

Item {
    id: expandedRoot
    property var widget: null

    implicitWidth: Kirigami.Units.gridUnit * 24
    implicitHeight: Kirigami.Units.gridUnit * 30

    Layout.minimumWidth: Kirigami.Units.gridUnit * 20
    Layout.minimumHeight: Kirigami.Units.gridUnit * 24
    Layout.preferredWidth: Kirigami.Units.gridUnit * 24
    Layout.preferredHeight: Kirigami.Units.gridUnit * 30
    Layout.maximumWidth: Kirigami.Units.gridUnit * 36
    Layout.maximumHeight: Kirigami.Units.gridUnit * 40

    function focusInput(): void {
        promptField.forceActiveFocus();
    }

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

    Component.onCompleted: {
        Plasmoid.checkReadiness();
        Qt.callLater(() => focusInput());
    }

    onVisibleChanged: {
        if (visible) {
            Plasmoid.checkReadiness();
            Plasmoid.refreshModels();
            Qt.callLater(() => focusInput());
        }
    }

    Shortcut {
        sequences: ["Ctrl+M", "Alt+M"]
        enabled: Plasmoid.whisperAvailable
        onActivated: Plasmoid.toggleRecording()
    }

    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (Plasmoid.state === "thinking") {
                Plasmoid.stopGeneration();
            } else if (Plasmoid.isSpeaking) {
                Plasmoid.stopSpeaking();
            } else if (expandedRoot.widget) {
                expandedRoot.widget.expanded = false;
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.smallSpacing

        // Assistant Status & Quick Controls Bar (Complements Plasma's top applet header without repeating title/pin)
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Rectangle {
                implicitWidth: 8
                implicitHeight: 8
                radius: 4
                color: {
                    if (!Plasmoid.ollamaReachable) {
                        return Kirigami.Theme.negativeTextColor;
                    }
                    if (Plasmoid.state === "listening") {
                        return "#00e5ff";
                    }
                    if (Plasmoid.wakeWordEnabled) {
                        return Kirigami.Theme.positiveTextColor;
                    }
                    return Kirigami.Theme.disabledTextColor;
                }
            }

            PlasmaComponents3.Label {
                Layout.fillWidth: true
                font: Kirigami.Theme.smallFont
                opacity: 0.85
                elide: Text.ElideRight
                text: {
                    if (!Plasmoid.ollamaReachable) {
                        return i18n("Sin conexión con Ollama");
                    }
                    if (Plasmoid.state === "listening") {
                        return i18n("Escuchando…");
                    }
                    if (Plasmoid.state === "transcribing") {
                        return i18n("Transcribiendo…");
                    }
                    if (Plasmoid.state === "thinking") {
                        return i18n("Pensando…");
                    }
                    if (Plasmoid.state === "speaking") {
                        return i18n("Hablando…");
                    }
                    if (Plasmoid.wakeWordEnabled) {
                        return i18n("«¡Hola Kiri!» activo");
                    }
                    return i18n("Listo");
                }
            }

            // Quick Toggle for "¡Hola Kiri!" Wake Word
            PlasmaComponents3.ToolButton {
                id: wakeWordButton
                visible: Plasmoid.whisperAvailable
                icon.name: Plasmoid.wakeWordEnabled ? "mic-on" : "mic-off"
                checkable: true
                checked: Plasmoid.wakeWordEnabled
                onClicked: {
                    const nextState = !Plasmoid.wakeWordEnabled;
                    Plasmoid.configuration.wakeWordEnabled = nextState;
                    Plasmoid.wakeWordEnabled = nextState;
                }
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: Plasmoid.wakeWordEnabled
                    ? i18n("Escucha activa «¡Hola Kiri!» activada (clic para desactivar)")
                    : i18n("Activar escucha manos libres «¡Hola Kiri!»")
            }

            // Speech Output Mode Selector Button
            PlasmaComponents3.ToolButton {
                id: speechModeButton
                icon.name: {
                    const mode = Plasmoid.configuration.speechOutputMode;
                    if (mode === "always") {
                        return "audio-volume-high-symbolic";
                    } else if (mode === "voice_only") {
                        return "audio-input-microphone-symbolic";
                    }
                    return "audio-volume-muted-symbolic";
                }
                checkable: false
                onClicked: {
                    const current = Plasmoid.configuration.speechOutputMode;
                    if (current === "manual") {
                        Plasmoid.configuration.speechOutputMode = "voice_only";
                    } else if (current === "voice_only") {
                        Plasmoid.configuration.speechOutputMode = "always";
                    } else {
                        Plasmoid.configuration.speechOutputMode = "manual";
                        Plasmoid.stopSpeaking();
                    }
                }
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: {
                    const mode = Plasmoid.configuration.speechOutputMode;
                    if (mode === "always") {
                        return i18n("Respuestas por voz: Hablar siempre en voz alta");
                    } else if (mode === "voice_only") {
                        return i18n("Respuestas por voz: Hablar cuando preguntes con la voz");
                    }
                    return i18n("Respuestas por voz: Solo manual (al pulsar el altavoz en un mensaje)");
                }
            }

            // Clear Conversation Button
            PlasmaComponents3.ToolButton {
                icon.name: "edit-clear-history-symbolic"
                visible: Plasmoid.messages.length > 0
                onClicked: Plasmoid.clearHistory()
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: i18n("Limpiar conversación")
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // Status / Readiness Inline Message
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !Plasmoid.ollamaReachable
            type: Kirigami.MessageType.Warning
            text: i18n("No se puede conectar con Ollama en %1. Ejecuta scripts/setup-models.sh o inicia ollama.service.", Plasmoid.configuration.ollamaUrl)
        }

        // Conversation History or Empty Welcome State
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Welcome placeholder when conversation is empty
            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width * 0.94
                visible: Plasmoid.messages.length === 0
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Heading {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    level: 2
                    text: i18n("¿En qué puedo ayudarte?")
                    wrapMode: Text.WordWrap
                }

                PlasmaComponents3.Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    opacity: 0.75
                    wrapMode: Text.WordWrap
                    text: i18n("Pulsa la esfera inferior o di «¡Hola Kiri!» para hablar, o elige una acción rápida:")
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Kirigami.Units.smallSpacing
                    rowSpacing: Kirigami.Units.smallSpacing
                    uniformCellWidths: true

                    Repeater {
                        model: [
                            { label: "🖥️ Analizar pantalla", prompt: "Analiza qué hay en mi pantalla" },
                            { label: "📋 Resumir copiado", prompt: "Resume lo que he copiado en el portapapeles" },
                            { label: "⚡ Estado del sistema", prompt: "Estado del sistema" },
                            { label: "🎵 ¿Qué canción suena?", prompt: "¿Qué canción está sonando?" },
                            { label: "🌙 Modo oscuro", prompt: "Activa el modo oscuro" },
                            { label: "🌐 Abrir navegador", prompt: "Abre el navegador" }
                        ]

                        PlasmaComponents3.Button {
                            required property var modelData
                            Layout.fillWidth: true
                            text: modelData.label
                            onClicked: Plasmoid.sendMessage(modelData.prompt, false)
                        }
                    }
                }
            }

            QQC2.ScrollView {
                anchors.fill: parent
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
                            radius: Kirigami.Units.mediumSpacing
                            color: Kirigami.Theme.alternateBackgroundColor
                            border.width: 1
                            border.color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.35)

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
                            radius: Kirigami.Units.mediumSpacing
                            color: msgDelegate.isUser
                                ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.22)
                                : Kirigami.Theme.alternateBackgroundColor
                            border.width: 1
                            border.color: msgDelegate.isUser
                                ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.45)
                                : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.12)

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
                                            color: Qt.rgba(0, 0, 0, 0.25)
                                            radius: Kirigami.Units.smallSpacing
                                            border.width: 1
                                            border.color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.4)

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

                                                PlasmaComponents3.Button {
                                                    icon.name: "media-playback-start-symbolic"
                                                    text: i18n("Ejecutar en Konsole")
                                                    onClicked: {
                                                        Plasmoid.executeCommandInKonsole(cmdCard.modelData);
                                                    }
                                                    QQC2.ToolTip.visible: hovered
                                                    QQC2.ToolTip.text: i18n("Ejecutar orden en una ventana interactiva de Konsole")
                                                }

                                                PlasmaComponents3.ToolButton {
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

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Kirigami.Units.smallSpacing

                                    PlasmaComponents3.Label {
                                        text: expandedRoot.formatTimestamp(msgDelegate.modelData.timestamp ?? "")
                                        font: Kirigami.Theme.smallFont
                                        opacity: 0.55
                                    }

                                    Item { Layout.fillWidth: true }

                                    // Speak / Stop button on Assistant responses
                                    PlasmaComponents3.ToolButton {
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
                                    PlasmaComponents3.ToolButton {
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

        // Dynamic status bar when recording, transcribing, thinking, or speaking (collapses when empty)
        PlasmaComponents3.Label {
            Layout.fillWidth: true
            visible: Plasmoid.statusMessage.length > 0
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: Plasmoid.statusMessage
            font: Kirigami.Theme.smallFont
            color: Plasmoid.state === "error" ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.highlightColor
            wrapMode: Text.WordWrap
        }

        // Siri Orb Centered Interactive Voice Control
        Item {
            Layout.fillWidth: true
            implicitHeight: Kirigami.Units.gridUnit * 4.0
            clip: true

            SiriOrbAnimation {
                id: siriOrb
                anchors.centerIn: parent
                assistantState: Plasmoid.state
                audioLevel: Plasmoid.audioLevel
                onClicked: {
                    if (Plasmoid.isSpeaking) {
                        Plasmoid.stopSpeaking();
                    } else if (Plasmoid.state === "thinking") {
                        Plasmoid.stopGeneration();
                    } else {
                        Plasmoid.toggleRecording();
                    }
                }
            }
        }

        // Bottom Text Input & Send Bar (Clean, without redundant mic button)
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            PlasmaComponents3.ScrollView {
                id: promptScrollView
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom
                Layout.maximumHeight: Kirigami.Units.gridUnit * 6
                QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

                PlasmaComponents3.TextArea {
                    id: promptField
                    focus: true
                    placeholderText: Plasmoid.isRecording
                        ? i18n("Escuchando tu voz…")
                        : i18n("Pregúntale o pídele cualquier cosa a Kiri…")
                    enabled: !Plasmoid.isRecording
                    wrapMode: TextEdit.Wrap

                    Keys.onPressed: event => {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ShiftModifier)) {
                            if (Plasmoid.state !== "thinking" && text.trim().length > 0) {
                                Plasmoid.sendMessage(text, false);
                                text = "";
                            }
                            event.accepted = true;
                        }
                    }
                }
            }

            PlasmaComponents3.Button {
                Layout.alignment: Qt.AlignBottom
                icon.name: Plasmoid.state === "thinking" ? "process-stop-symbolic" : "document-send-symbolic"
                enabled: Plasmoid.state === "thinking" || promptField.text.trim().length > 0
                onClicked: {
                    if (Plasmoid.state === "thinking") {
                        Plasmoid.stopGeneration();
                    } else if (promptField.text.trim().length > 0) {
                        Plasmoid.sendMessage(promptField.text, false);
                        promptField.text = "";
                    }
                }
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: Plasmoid.state === "thinking" ? i18n("Detener generación") : i18n("Enviar mensaje")
            }
        }
    }
}
