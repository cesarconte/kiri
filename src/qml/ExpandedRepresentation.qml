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
import org.kde.plasma.extras as PlasmaExtras
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

    Component.onCompleted: {
        Plasmoid.checkReadiness();
        Qt.callLater(() => focusInput());
    }

    onVisibleChanged: {
        if (visible) {
            Plasmoid.checkReadiness();
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

        // Main Plasma Navigation Bar
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.ComboBox {
                id: modelCombo
                Layout.fillWidth: true
                model: Plasmoid.availableModels.length > 0
                    ? Plasmoid.availableModels
                    : [Plasmoid.configuration.ollamaModel]
                currentIndex: {
                    let idx = model.indexOf(Plasmoid.configuration.ollamaModel);
                    if (idx < 0 || (Plasmoid.configuration.ollamaModel && Plasmoid.configuration.ollamaModel.indexOf("llama3.2") !== -1)) {
                        const qwenIdx = model.indexOf("qwen2.5:7b");
                        if (qwenIdx >= 0) {
                            return qwenIdx;
                        }
                    }
                    return idx >= 0 ? idx : 0;
                }
                onActivated: index => {
                    Plasmoid.configuration.ollamaModel = textAt(index);
                }
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: i18n("Active Ollama model")
            }

            PlasmaComponents3.ToolButton {
                icon.name: "view-refresh-symbolic"
                onClicked: Plasmoid.refreshModels()
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: i18n("Refresh Ollama models")
            }

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
                        return i18n("Voice responses: Always speak answers aloud");
                    } else if (mode === "voice_only") {
                        return i18n("Voice responses: Speak when asked by voice");
                    }
                    return i18n("Voice responses: Manual only (click Speak on a message)");
                }
            }

            PlasmaComponents3.ToolButton {
                icon.name: "edit-clear-history-symbolic"
                enabled: Plasmoid.messages.length > 0
                onClicked: Plasmoid.clearHistory()
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: i18n("Clear conversation")
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
            text: i18n("Ollama is not reachable at %1. Run scripts/setup-models.sh or start ollama.service.", Plasmoid.configuration.ollamaUrl)
        }

        // Conversation History or Empty Welcome State
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Welcome placeholder when conversation is empty
            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width * 0.85
                visible: Plasmoid.messages.length === 0
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Heading {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    level: 2
                    text: i18n("How can I help you?")
                    wrapMode: Text.WordWrap
                }

                PlasmaComponents3.Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    opacity: 0.75
                    wrapMode: Text.WordWrap
                    text: i18n("Click the glowing orb below to speak with your voice, or type your question in the box.")
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

                    delegate: Item {
                        id: msgDelegate
                        required property var modelData
                        required property int index

                        width: chatListView.width
                        implicitHeight: bubbleRect.height

                        readonly property bool isUser: modelData.role === "user"
                        readonly property var detectedCmds: (!isUser && !modelData.streaming)
                            ? Plasmoid.extractTerminalCommands(modelData.content)
                            : []

                        Rectangle {
                            id: bubbleRect
                            width: msgDelegate.isUser
                                ? Math.min(parent.width * 0.88, Math.max(Kirigami.Units.gridUnit * 8, msgColumn.implicitWidth + Kirigami.Units.mediumSpacing * 2))
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
                                    textFormat: {
                                        if (msgDelegate.isUser || msgDelegate.modelData.streaming) {
                                            return TextEdit.PlainText;
                                        }
                                        return TextEdit.RichText;
                                    }
                                    onLinkActivated: link => Qt.openUrlExternally(link)
                                    text: {
                                        if (msgDelegate.modelData.streaming) {
                                            return Plasmoid.streamingText.length > 0 ? Plasmoid.streamingText : "…"
                                        }
                                        if (msgDelegate.isUser) {
                                            return msgDelegate.modelData.content;
                                        }
                                        return Plasmoid.renderMarkdown(msgDelegate.modelData.content);
                                    }

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
                                        text: msgDelegate.modelData.timestamp ?? ""
                                        font: Kirigami.Theme.smallFont
                                        opacity: 0.6
                                    }

                                    Item { Layout.fillWidth: true }

                                    // Speak / Stop button on Assistant responses
                                    PlasmaComponents3.ToolButton {
                                        visible: !msgDelegate.isUser && !msgDelegate.modelData.streaming && msgDelegate.modelData.content.length > 0
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
                                        QQC2.ToolTip.text: Plasmoid.isSpeaking ? i18n("Stop speaking") : i18n("Read aloud")
                                    }

                                    // Copy message button
                                    PlasmaComponents3.ToolButton {
                                        visible: !msgDelegate.modelData.streaming && msgDelegate.modelData.content.length > 0
                                        icon.name: "edit-copy-symbolic"
                                        flat: true
                                        implicitWidth: Kirigami.Units.iconSizes.smallMedium
                                        implicitHeight: Kirigami.Units.iconSizes.smallMedium
                                        onClicked: {
                                            msgText.selectAll();
                                            msgText.copy();
                                            msgText.deselect();
                                        }
                                        QQC2.ToolTip.visible: hovered
                                        QQC2.ToolTip.text: i18n("Copy text")
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Dynamic status bar when recording, transcribing, thinking, or speaking
        PlasmaComponents3.Label {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 1.2
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: Plasmoid.statusMessage
            font: Kirigami.Theme.smallFont
            color: Plasmoid.state === "error" ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.highlightColor
            opacity: Plasmoid.statusMessage.length > 0 ? 1.0 : 0.0
            wrapMode: Text.WordWrap

            Behavior on opacity {
                NumberAnimation { duration: 150 }
            }
        }

        // Siri Orb Centered Interactive Control
        Item {
            Layout.fillWidth: true
            implicitHeight: Kirigami.Units.gridUnit * 4.2
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

        // Bottom Text Input & Send Bar
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            PlasmaComponents3.ToolButton {
                id: inputMicButton
                Layout.alignment: Qt.AlignBottom
                visible: Plasmoid.whisperAvailable
                icon.name: Plasmoid.isRecording ? "media-playback-stop-symbolic" : "audio-input-microphone-symbolic"
                flat: true
                onClicked: Plasmoid.toggleRecording()
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: Plasmoid.isRecording
                    ? i18n("Stop listening (Ctrl+M)")
                    : i18n("Listen by voice (Ctrl+M)")
            }

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
                        ? i18n("Listening to your voice…")
                        : i18n("Ask Kiri anything…")
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
                QQC2.ToolTip.text: Plasmoid.state === "thinking" ? i18n("Stop generation") : i18n("Send message")
            }
        }
    }
}
