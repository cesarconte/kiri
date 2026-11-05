/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Window

import org.kde.kirigami as Kirigami

Window {
    id: root

    title: i18n("Kity")
    width: 540
    height: 740
    minimumWidth: 440
    minimumHeight: 560
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint

    property bool isPinned: false
    property bool showWritingToolsMenu: false
    property bool showSettingsModal: false

    onIsPinnedChanged: {
        if (isPinned) {
            root.flags = Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint;
        } else {
            root.flags = Qt.Window | Qt.FramelessWindowHint;
        }
        root.show();
    }

    // Outer Liquid Glass Container
    Rectangle {
        id: glassContainer
        anchors.fill: parent
        anchors.margins: 10
        radius: 20
        color: Kirigami.Theme.colorSet === Kirigami.Theme.Window && Kirigami.Theme.backgroundColor.hslLightness < 0.5
               ? Qt.rgba(0.08, 0.09, 0.12, 0.82)
               : Qt.rgba(0.96, 0.97, 0.99, 0.85)

        border.width: 1
        border.color: Kirigami.Theme.colorSet === Kirigami.Theme.Window && Kirigami.Theme.backgroundColor.hslLightness < 0.5
                      ? Qt.rgba(1, 1, 1, 0.14)
                      : Qt.rgba(0, 0, 0, 0.12)

        // Specular glow highlight along top edge
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 1
            height: 1
            color: Qt.rgba(1, 1, 1, 0.35)
            radius: 20
        }

        // =====================================================================
        // 1. macOS Golden Gate Header Bar
        // =====================================================================
        Item {
            id: headerBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 48

            // Window Drag Area
            DragHandler {
                target: null
                onActiveChanged: {
                    if (active) {
                        root.startSystemMove();
                    }
                }
            }

            // Left: macOS Traffic Lights
            Row {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                // Close (red)
                Rectangle {
                    width: 12
                    height: 12
                    radius: 6
                    color: closeHover.hovered ? "#ff5f56" : "#ff5f57"
                    border.width: 0.5
                    border.color: "#e0443e"

                    HoverHandler { id: closeHover }
                    TapHandler {
                        onTapped: root.close()
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "×"
                        font.pixelSize: 10
                        font.bold: true
                        color: "#4d0000"
                        visible: closeHover.hovered
                    }
                }

                // Minimize (yellow)
                Rectangle {
                    width: 12
                    height: 12
                    radius: 6
                    color: minHover.hovered ? "#ffbd2e" : "#febc2e"
                    border.width: 0.5
                    border.color: "#dea123"

                    HoverHandler { id: minHover }
                    TapHandler {
                        onTapped: root.showMinimized()
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "–"
                        font.pixelSize: 10
                        font.bold: true
                        color: "#593b00"
                        visible: minHover.hovered
                    }
                }

                // Maximize / Compact toggle (green)
                Rectangle {
                    width: 12
                    height: 12
                    radius: 6
                    color: maxHover.hovered ? "#27c93f" : "#28c840"
                    border.width: 0.5
                    border.color: "#1aab29"

                    HoverHandler { id: maxHover }
                    TapHandler {
                        onTapped: {
                            if (root.visibility === Window.Maximized) {
                                root.showNormal();
                            } else {
                                root.showMaximized();
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "+"
                        font.pixelSize: 10
                        font.bold: true
                        color: "#004d11"
                        visible: maxHover.hovered
                    }
                }
            }

            // Center: Iridescent Siri Pill & Live Status
            Rectangle {
                anchors.centerIn: parent
                height: 28
                width: centerContent.implicitWidth + 24
                radius: 14
                color: Qt.rgba(1, 1, 1, 0.08)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.12)

                RowLayout {
                    id: centerContent
                    anchors.centerIn: parent
                    spacing: 8

                    SiriOrbAnimation {
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        assistantState: kityClient.state
                        audioLevel: kityClient.audioLevel
                    }

                    Text {
                        text: i18n("Kity")
                        font.bold: true
                        font.pixelSize: 13
                        color: Kirigami.Theme.textColor
                    }

                    Rectangle {
                        width: 4
                        height: 4
                        radius: 2
                        color: Kirigami.Theme.textColor
                        opacity: 0.4
                    }

                    Text {
                        text: {
                            if (kityClient.state === "listening") return i18n("Escuchando…");
                            if (kityClient.state === "transcribing") return i18n("Transcribiendo…");
                            if (kityClient.state === "thinking") return i18n("Pensando…");
                            if (kityClient.state === "speaking") return i18n("Hablando…");
                            return i18n("Listo");
                        }
                        font.pixelSize: 12
                        opacity: 0.75
                        color: Kirigami.Theme.textColor
                    }
                }
            }

            // Right: Header Tools (New Chat, Pin on Top, Settings)
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                // New Chat
                QQC2.ToolButton {
                    icon.name: "document-new"
                    QQC2.ToolTip.text: i18n("Nueva conversación")
                    QQC2.ToolTip.visible: hovered
                    onClicked: kityClient.clearHistory()
                }

                // Pin Window On Top
                QQC2.ToolButton {
                    icon.name: root.isPinned ? "window-pin" : "window-unpin"
                    checkable: true
                    checked: root.isPinned
                    QQC2.ToolTip.text: root.isPinned ? i18n("Desanclar de primer plano") : i18n("Mantener siempre encima")
                    QQC2.ToolTip.visible: hovered
                    onClicked: root.isPinned = !root.isPinned
                }

                // Settings
                QQC2.ToolButton {
                    icon.name: "configure"
                    QQC2.ToolTip.text: i18n("Preferencias de Kity")
                    QQC2.ToolTip.visible: hovered
                    onClicked: root.showSettingsModal = !root.showSettingsModal
                }
            }
        }

        // Horizontal separator line below header
        Rectangle {
            anchors.top: headerBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Qt.rgba(1, 1, 1, 0.08)
        }

        // =====================================================================
        // 2. Main Conversational Body
        // =====================================================================
        Item {
            id: contentArea
            anchors.top: headerBar.bottom
            anchors.bottom: footerCapsule.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 12

            // View A: Welcome Screen when conversation is empty
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 18
                width: Math.min(parent.width * 0.9, 440)
                visible: kityClient.messages.length === 0

                SiriOrbAnimation {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 84
                    Layout.preferredHeight: 84
                    assistantState: kityClient.state
                    audioLevel: kityClient.audioLevel

                    TapHandler {
                        onTapped: kityClient.toggleRecording()
                    }
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: i18n("¿En qué puedo ayudarte hoy?")
                    font.pixelSize: 20
                    font.bold: true
                    color: Kirigami.Theme.textColor
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: i18n("Pulsa el orbe para hablar o elige una acción:")
                    font.pixelSize: 13
                    opacity: 0.65
                    color: Kirigami.Theme.textColor
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 10

                    // Suggestion 1: Screen Analysis
                    Rectangle {
                        Layout.fillWidth: true
                        height: 52
                        radius: 12
                        color: Qt.rgba(1, 1, 1, s1Hover.hovered ? 0.12 : 0.06)
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.1)

                        HoverHandler { id: s1Hover }
                        TapHandler {
                            onTapped: kityClient.runVisualIntelligence("region", "ocr")
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8
                            Text { text: "🖥️"; font.pixelSize: 18 }
                            Text {
                                Layout.fillWidth: true
                                text: i18n("Analizar pantalla")
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                color: Kirigami.Theme.textColor
                                elide: Text.ElideRight
                            }
                        }
                    }

                    // Suggestion 2: Writing Tools
                    Rectangle {
                        Layout.fillWidth: true
                        height: 52
                        radius: 12
                        color: Qt.rgba(1, 1, 1, s2Hover.hovered ? 0.12 : 0.06)
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.1)

                        HoverHandler { id: s2Hover }
                        TapHandler {
                            onTapped: kityClient.applyWritingTool("proofread")
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8
                            Text { text: "✍️"; font.pixelSize: 18 }
                            Text {
                                Layout.fillWidth: true
                                text: i18n("Corregir portapapeles")
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                color: Kirigami.Theme.textColor
                                elide: Text.ElideRight
                            }
                        }
                    }

                    // Suggestion 3: Translate
                    Rectangle {
                        Layout.fillWidth: true
                        height: 52
                        radius: 12
                        color: Qt.rgba(1, 1, 1, s3Hover.hovered ? 0.12 : 0.06)
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.1)

                        HoverHandler { id: s3Hover }
                        TapHandler {
                            onTapped: kityClient.applyWritingTool("translate:es")
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8
                            Text { text: "🌐"; font.pixelSize: 18 }
                            Text {
                                Layout.fillWidth: true
                                text: i18n("Traducir al español")
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                color: Kirigami.Theme.textColor
                                elide: Text.ElideRight
                            }
                        }
                    }

                    // Suggestion 4: Knowledge / Notes
                    Rectangle {
                        Layout.fillWidth: true
                        height: 52
                        radius: 12
                        color: Qt.rgba(1, 1, 1, s4Hover.hovered ? 0.12 : 0.06)
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.1)

                        HoverHandler { id: s4Hover }
                        TapHandler {
                            kityClient.sendMessage(i18n("¿Qué información tienes en mis notas locales?"), false)
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8
                            Text { text: "💡"; font.pixelSize: 18 }
                            Text {
                                Layout.fillWidth: true
                                text: i18n("Consultar mis notas")
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                color: Kirigami.Theme.textColor
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            // View B: Conversation Chat Message List
            ListView {
                id: messageListView
                anchors.fill: parent
                visible: kityClient.messages.length > 0
                clip: true
                spacing: 14
                model: kityClient.messages

                onCountChanged: {
                    Qt.callLater(() => messageListView.positionViewAtEnd());
                }

                delegate: Item {
                    id: msgDelegate
                    required property var modelData
                    required property int index

                    readonly property bool isUser: modelData.sender === "user"
                    readonly property string msgText: modelData.text || ""
                    readonly property string timestamp: modelData.timestamp || ""

                    width: messageListView.width
                    height: bubbleCol.implicitHeight + 6

                    ColumnLayout {
                        id: bubbleCol
                        width: parent.width
                        spacing: 4

                        // Message Bubble Card
                        Rectangle {
                            Layout.alignment: msgDelegate.isUser ? Qt.AlignRight : Qt.AlignLeft
                            Layout.maximumWidth: Math.min(messageListView.width * 0.82, 420)
                            implicitWidth: textContent.implicitWidth + 24
                            implicitHeight: textContent.implicitHeight + 20
                            radius: 16

                            color: msgDelegate.isUser
                                   ? (Kirigami.Theme.highlightColor || "#007aff")
                                   : Qt.rgba(1, 1, 1, 0.08)

                            border.width: msgDelegate.isUser ? 0 : 1
                            border.color: Qt.rgba(1, 1, 1, 0.12)

                            ColumnLayout {
                                id: textContent
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 6

                                TextEdit {
                                    id: bodyText
                                    Layout.fillWidth: true
                                    readOnly: true
                                    selectByMouse: true
                                    wrapMode: TextEdit.Wrap
                                    font.pixelSize: 13.5
                                    textFormat: msgDelegate.isUser ? TextEdit.PlainText : TextEdit.MarkdownText
                                    text: msgDelegate.msgText
                                    color: msgDelegate.isUser ? "#ffffff" : Kirigami.Theme.textColor
                                }

                                // Assistant Tools Row (Copy, Read aloud)
                                RowLayout {
                                    visible: !msgDelegate.isUser
                                    Layout.fillWidth: true
                                    spacing: 8

                                    QQC2.ToolButton {
                                        icon.name: "edit-copy"
                                        icon.width: 14
                                        icon.height: 14
                                        QQC2.ToolTip.text: i18n("Copiar al portapapeles")
                                        QQC2.ToolTip.visible: hovered
                                        onClicked: kityClient.copyToClipboard(msgDelegate.msgText)
                                    }

                                    QQC2.ToolButton {
                                        icon.name: "audio-volume-high"
                                        icon.width: 14
                                        icon.height: 14
                                        QQC2.ToolTip.text: i18n("Escuchar respuesta con voz")
                                        QQC2.ToolTip.visible: hovered
                                        onClicked: kityClient.speakText(msgDelegate.msgText)
                                    }

                                    Item { Layout.fillWidth: true }

                                    Text {
                                        text: msgDelegate.timestamp.length >= 16 ? msgDelegate.timestamp.substring(11, 16) : ""
                                        font.pixelSize: 10.5
                                        opacity: 0.5
                                        color: Kirigami.Theme.textColor
                                    }
                                }
                            }
                        }
                    }
                }

                // Streaming Assistant Reply Indicator
                footer: Item {
                    width: messageListView.width
                    height: (kityClient.state === "thinking" || kityClient.streamingText.length > 0) ? streamCol.implicitHeight + 12 : 0
                    visible: kityClient.state === "thinking" || kityClient.streamingText.length > 0

                    ColumnLayout {
                        id: streamCol
                        anchors.fill: parent
                        spacing: 4

                        Rectangle {
                            Layout.alignment: Qt.AlignLeft
                            Layout.maximumWidth: Math.min(messageListView.width * 0.82, 420)
                            implicitWidth: streamContent.implicitWidth + 24
                            implicitHeight: streamContent.implicitHeight + 20
                            radius: 16
                            color: Qt.rgba(1, 1, 1, 0.08)
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.12)

                            ColumnLayout {
                                id: streamContent
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 6

                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 13.5
                                    text: kityClient.streamingText.length > 0
                                          ? kityClient.streamingText + " ▍"
                                          : i18n("Pensando…")
                                    color: Kirigami.Theme.textColor
                                }

                                QQC2.Button {
                                    text: i18n("Detener")
                                    icon.name: "process-stop"
                                    onClicked: kityClient.stopGeneration()
                                }
                            }
                        }
                    }
                }
            }
        }

        // =====================================================================
        // 3. Floating Liquid Glass Footer Input Capsule
        // =====================================================================
        Rectangle {
            id: footerCapsule
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 14
            height: Math.max(52, inputArea.implicitHeight + 16)
            radius: 26

            color: Qt.rgba(1, 1, 1, 0.10)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.16)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 10
                spacing: 8

                // Live Text / OCR Button
                QQC2.ToolButton {
                    icon.name: "rectangular-snip-symbolic"
                    QQC2.ToolTip.text: i18n("Extraer texto de pantalla (Live Text OCR)")
                    QQC2.ToolTip.visible: hovered
                    onClicked: kityClient.runVisualIntelligence("region", "ocr")
                }

                // Writing Tools Button
                QQC2.ToolButton {
                    icon.name: "document-edit-symbolic"
                    QQC2.ToolTip.text: i18n("Herramientas de escritura")
                    QQC2.ToolTip.visible: hovered
                    onClicked: writingToolsMenu.open()

                    QQC2.Menu {
                        id: writingToolsMenu
                        y: -height - 6

                        QQC2.MenuItem {
                            text: i18n("✍️ Corregir ortografía (Proofread)")
                            onTriggered: kityClient.applyWritingTool("proofread")
                        }
                        QQC2.MenuItem {
                            text: i18n("📝 Reescribir con fluidez")
                            onTriggered: kityClient.applyWritingTool("rewrite")
                        }
                        QQC2.MenuItem {
                            text: i18n("👔 Tono profesional")
                            onTriggered: kityClient.applyWritingTool("professional")
                        }
                        QQC2.MenuItem {
                            text: i18n("😊 Tono amigable")
                            onTriggered: kityClient.applyWritingTool("friendly")
                        }
                        QQC2.MenuItem {
                            text: i18n("📊 Resumir texto")
                            onTriggered: kityClient.applyWritingTool("summary")
                        }
                    }
                }

                // Multi-line Text Input
                QQC2.ScrollView {
                    Layout.fillWidth: true
                    Layout.maximumHeight: 90

                    QQC2.TextArea {
                        id: inputArea
                        placeholderText: kityClient.isRecording
                                         ? i18n("Escuchando tu voz…")
                                         : i18n("Pregúntale o pídele cualquier cosa a Kity…")
                        wrapMode: TextEdit.Wrap
                        font.pixelSize: 13.5
                        focus: true

                        Keys.onPressed: event => {
                            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ShiftModifier)) {
                                if (text.trim().length > 0) {
                                    kityClient.sendMessage(text.trim(), false);
                                    text = "";
                                }
                                event.accepted = true;
                            }
                        }
                    }
                }

                // Siri Orb Voice / Stop Action
                SiriOrbAnimation {
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 36
                    assistantState: kityClient.state
                    audioLevel: kityClient.audioLevel

                    TapHandler {
                        onTapped: {
                            if (kityClient.isRecording) {
                                kityClient.stopRecording();
                            } else if (kityClient.isSpeaking) {
                                kityClient.stopSpeaking();
                            } else if (kityClient.isBusy) {
                                kityClient.stopGeneration();
                            } else {
                                kityClient.startRecording();
                            }
                        }
                    }
                }
            }
        }

        // =====================================================================
        // 4. Slide-in Settings Modal
        // =====================================================================
        Rectangle {
            id: settingsOverlay
            anchors.fill: parent
            radius: 20
            color: Qt.rgba(0.04, 0.05, 0.07, 0.94)
            visible: root.showSettingsModal
            opacity: visible ? 1 : 0

            Behavior on opacity {
                NumberAnimation { duration: 180 }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 16

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: i18n("Preferencias de Kity")
                        font.pixelSize: 18
                        font.bold: true
                        color: "#ffffff"
                    }
                    Item { Layout.fillWidth: true }
                    QQC2.ToolButton {
                        text: "✕"
                        onClicked: root.showSettingsModal = false
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Qt.rgba(1, 1, 1, 0.15)
                }

                // Readiness Status
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: i18n("Estado del sistema:")
                        font.bold: true
                        color: "#ffffff"
                    }

                    Text {
                        text: "• Ollama LLM: " + (kityClient.ollamaReachable ? "✓ Conectado" : "✗ Desconectado")
                        color: kityClient.ollamaReachable ? "#4caf50" : "#f44336"
                    }
                    Text {
                        text: "• Whisper STT (Voz): " + (kityClient.whisperAvailable ? "✓ Disponible" : "✗ No encontrado")
                        color: kityClient.whisperAvailable ? "#4caf50" : "#f44336"
                    }
                    Text {
                        text: "• Piper TTS (Voz): " + (kityClient.piperAvailable ? "✓ Disponible" : "✗ No encontrado")
                        color: kityClient.piperAvailable ? "#4caf50" : "#f44336"
                    }
                }

                QQC2.CheckBox {
                    text: i18n("Escucha activa manos libres («¡Hola Kity!»)")
                    checked: kityClient.wakeWordEnabled
                    onToggled: kityClient.wakeWordEnabled = checked
                }

                QQC2.Button {
                    icon.name: "document-open-folder"
                    text: i18n("Abrir carpeta de notas locales (RAG)")
                    onClicked: kityClient.openKnowledgeFolder()
                }

                Item { Layout.fillHeight: true }

                QQC2.Button {
                    Layout.alignment: Qt.AlignRight
                    text: i18n("Cerrar")
                    onClicked: root.showSettingsModal = false
                }
            }
        }
    }

    // Window Resizer Corner
    Item {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 18
        height: 18

        DragHandler {
            target: null
            onActiveChanged: {
                if (active) {
                    root.startSystemResize(Qt.BottomEdge | Qt.RightEdge);
                }
            }
        }
    }
}
