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
import CachyOS.UI

import "cards" as Cards

CachyWindow {
    id: root

    title: i18n("Kity")
    width: compactMode ? (compactWorking ? 310 : (compactAnswerVisible ? 400 : 420)) : 860
    height: compactMode ? (compactWorking ? 62 : (compactAnswerVisible ? 520 : 272)) : 600
    color: "transparent"

    showTrafficLights: !root.compactMode
    showTitleBarDivider: !root.compactMode
    showWindowFrame: true
    sidebarVisible: !root.compactMode && !root.showPreferences
    sidebarWidth: 272
    cornerRadius: root.compactMode ? (root.compactWorking ? 31 : 24) : CachyTheme.radiusWindow
    backgroundColor: root.compactMode ? Qt.rgba(0.07, 0.08, 0.11, 0.90) : "transparent"

    property bool isPinned: false
    property bool compactMode: true
    property bool compactAnswerVisible: false
    readonly property bool compactWorking: root.compactMode && (kityClient.state === "thinking" || kityClient.state === "transcribing")

    property bool showWritingTools: false
    property bool showVisualIntelligence: false
    property bool showMathNotes: false
    property bool showPreferences: false
    property bool showSearchFilter: false
    property string searchQuery: ""
    property var mathResults: []
    property string visualCaptureScope: "region"

    readonly property bool isVoiceActive: kityClient.state === "listening" || kityClient.state === "thinking" || kityClient.state === "speaking"
    readonly property bool isDark: CachyTheme.isDark

    minimumWidth: compactMode ? 300 : 680
    minimumHeight: compactMode ? 60 : 480

    property real animTime: 0.0
    NumberAnimation on animTime {
        from: 0.0
        to: 1000.0
        duration: 1000000
        loops: Animation.Infinite
        running: true
    }

    onIsPinnedChanged: updateWindowFlags()
    onCompactModeChanged: {
        if (!compactMode) {
            root.compactAnswerVisible = false;
        }
    }

    function updateWindowFlags() {
        if (isPinned) {
            root.flags = Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint;
        } else {
            root.flags = Qt.Window | Qt.FramelessWindowHint;
        }
        root.show();
    }

    // Shortcuts
    Shortcut {
        sequences: ["Ctrl+M", "Alt+M"]
        enabled: kityClient.whisperAvailable
        onActivated: kityClient.toggleRecording()
    }

    Shortcut {
        sequences: ["Ctrl+1", "Alt+1"]
        enabled: root.compactMode
        onActivated: kityClient.launchSpotlightCategory("applications")
    }
    Shortcut {
        sequences: ["Ctrl+2", "Alt+2"]
        enabled: root.compactMode
        onActivated: kityClient.launchSpotlightCategory("files")
    }
    Shortcut {
        sequences: ["Ctrl+3", "Alt+3"]
        enabled: root.compactMode
        onActivated: {
            root.compactAnswerVisible = true;
            kityClient.launchSpotlightCategory("actions");
        }
    }
    Shortcut {
        sequences: ["Ctrl+4", "Alt+4"]
        enabled: root.compactMode
        onActivated: {
            root.compactAnswerVisible = true;
            kityClient.launchSpotlightCategory("clipboard");
        }
    }

    Shortcut {
        sequences: ["Ctrl+N"]
        onActivated: kityClient.newConversation()
    }

    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (root.compactMode) {
                if (root.compactAnswerVisible) {
                    root.compactAnswerVisible = false;
                } else {
                    root.hide();
                }
            } else if (root.showPreferences) {
                root.showPreferences = false;
            } else if (kityClient.state === "thinking") {
                kityClient.stopGeneration();
            } else if (kityClient.isSpeaking) {
                kityClient.stopSpeaking();
            } else {
                root.close();
            }
        }
    }

    // =========================================================================
    // 1. GOLDEN GATE EDGE-TO-EDGE SIDEBAR ("The Siri app" - 09_the_siri_app_window.png)
    // =========================================================================
    sidebarHeaderActions: [
        Row {
            spacing: 6
            anchors.verticalCenter: parent.verticalCenter

            // View Mode Toggle (2-column Grid vs 1-column List)
            CachyToolbarButton {
                text: kityClient.sidebarViewMode === "grid" ? "⊞" : "☰"
                tooltipText: kityClient.sidebarViewMode === "grid"
                    ? i18n("Cambiar a vista de lista")
                    : i18n("Cambiar a cuadrícula de 2 columnas (Golden Gate)")
                onClicked: {
                    kityClient.sidebarViewMode = (kityClient.sidebarViewMode === "grid") ? "list" : "grid";
                }
            }
        }
    ]

    sidebar: [
        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            anchors.bottomMargin: 12
            spacing: 8

            // Optional Sidebar Search Filter
            CachyTextField {
                visible: root.showSearchFilter
                Layout.fillWidth: true
                isSearch: true
                placeholderText: i18n("Filter chats...")
                text: root.searchQuery
                onTextChanged: root.searchQuery = text
            }

            // Scrollable Conversation History Cards (2-column Grid or List)
            QQC2.ScrollView {
                id: sidebarScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff
                clip: true

                GridLayout {
                    width: sidebarScroll.availableWidth
                    columns: kityClient.sidebarViewMode === "grid" ? 2 : 1
                    columnSpacing: 10
                    rowSpacing: 10

                    Repeater {
                        model: kityClient.conversations

                        Rectangle {
                            id: convCard
                            required property var modelData
                            required property int index

                            readonly property bool isCurrent: modelData.id === kityClient.activeConversationId
                            readonly property bool matchesSearch: root.searchQuery.trim().length === 0
                                || (modelData.title && modelData.title.toLowerCase().indexOf(root.searchQuery.toLowerCase()) !== -1)
                                || (modelData.preview && modelData.preview.toLowerCase().indexOf(root.searchQuery.toLowerCase()) !== -1)

                            visible: matchesSearch
                            Layout.fillWidth: true
                            Layout.preferredWidth: kityClient.sidebarViewMode === "grid" ? (sidebarScroll.availableWidth - 10) / 2 : sidebarScroll.availableWidth
                            implicitHeight: convCardCol.implicitHeight + 22
                            radius: 16

                            color: convCard.isCurrent
                                ? (CachyTheme.isDark ? Qt.rgba(1, 1, 1, 0.13) : Qt.rgba(1, 1, 1, 0.88))
                                : (convHover.hovered ? CachyTheme.cardBackgroundHover : CachyTheme.cardBackground)

                            border.width: convCard.isCurrent ? 2 : 1
                            border.color: convCard.isCurrent ? "#007AFF" : CachyTheme.specularBorder

                            Behavior on color { ColorAnimation { duration: 120 } }

                            HoverHandler {
                                id: convHover
                                cursorShape: Qt.PointingHandCursor
                            }

                            TapHandler {
                                onTapped: {
                                    root.showPreferences = false;
                                    kityClient.selectConversation(convCard.modelData.id);
                                }
                            }

                            ColumnLayout {
                                id: convCardCol
                                anchors.fill: parent
                                anchors.margins: 11
                                spacing: 4

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        text: convCard.modelData.timeLabel || "Today"
                                        font.pixelSize: 10
                                        color: CachyTheme.textMuted
                                        elide: Text.ElideRight
                                    }

                                    Kirigami.Icon {
                                        visible: convCard.modelData.pinned === true || convHover.hovered
                                        source: "window-pin"
                                        Layout.preferredWidth: 13
                                        Layout.preferredHeight: 13
                                        isMask: true
                                        color: CachyTheme.textSecondary
                                        opacity: convCard.modelData.pinned ? 0.95 : 0.45

                                        TapHandler {
                                            onTapped: kityClient.togglePinConversation(convCard.modelData.id)
                                        }
                                    }
                                }

                                QQC2.Label {
                                    Layout.fillWidth: true
                                    text: convCard.modelData.title || i18n("Kity AI")
                                    font.pixelSize: 13
                                    font.bold: true
                                    color: CachyTheme.textPrimary
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }

                                QQC2.Label {
                                    Layout.fillWidth: true
                                    text: convCard.modelData.preview || ""
                                    font.pixelSize: 11
                                    color: CachyTheme.textSecondary
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: kityClient.previewLines
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
            }
        }
    ]

    // =========================================================================
    // 2. HEADER TOOLBAR (Main Conversation Pane - 09_the_siri_app_window.png)
    // =========================================================================
    headerLeading: [
        Row {
            spacing: 6
            anchors.verticalCenter: parent.verticalCenter

            // Golden Gate New Chat Button (Square + Pencil compose glyph)
            CachyToolbarButton {
                iconName: "document-new-symbolic"
                text: "✎"
                tooltipText: i18n("Nueva conversación (Ctrl+N)")
                onClicked: {
                    root.showPreferences = false;
                    kityClient.newConversation();
                }
            }

            // Writing Tools Quick Button
            CachyToolbarButton {
                iconName: "document-edit-symbolic"
                text: "✦"
                checkable: true
                checked: root.showWritingTools
                tooltipText: i18n("Herramientas de Escritura")
                onClicked: {
                    root.showWritingTools = !root.showWritingTools;
                    if (root.showWritingTools) {
                        root.showVisualIntelligence = false;
                        root.showMathNotes = false;
                    }
                }
            }

            // Visual Intelligence Quick Button
            CachyToolbarButton {
                iconName: "camera-photo-symbolic"
                text: "⌖"
                checkable: true
                checked: root.showVisualIntelligence
                tooltipText: i18n("Inteligencia Visual (Spectacle + OCR)")
                onClicked: {
                    root.showVisualIntelligence = !root.showVisualIntelligence;
                    if (root.showVisualIntelligence) {
                        root.showWritingTools = false;
                        root.showMathNotes = false;
                    }
                }
            }

            // Math Notes Quick Button
            CachyToolbarButton {
                text: "∑"
                checkable: true
                checked: root.showMathNotes
                tooltipText: i18n("Notas Matemáticas")
                onClicked: {
                    root.showMathNotes = !root.showMathNotes;
                    if (root.showMathNotes) {
                        root.showWritingTools = false;
                        root.showVisualIntelligence = false;
                    }
                }
            }
        }
    ]

    // Center: Glowing Iridescent Kity Pill (Aligned on 22px midline)
    headerCenter: [
        Rectangle {
            anchors.centerIn: parent
            height: 26
            width: siriPillRow.implicitWidth + 20
            radius: 13
            color: CachyTheme.surfacePill
            border.width: 1
            border.color: CachyTheme.specularBorder

            RowLayout {
                id: siriPillRow
                anchors.centerIn: parent
                spacing: 6

                SiriOrbAnimation {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    assistantState: kityClient.state
                    audioLevel: kityClient.audioLevel
                }

                QQC2.Label {
                    text: i18n("Kity")
                    font.bold: true
                    font.pixelSize: 13
                    color: CachyTheme.textPrimary
                }
            }
        }
    ]

    // Trailing Actions: Search, Text Size, Pin, and Preferences (Rightmost)
    headerActions: [
        Row {
            spacing: 4
            anchors.verticalCenter: parent.verticalCenter

            // Search button (top-right of Golden Gate Siri App)
            QQC2.ToolButton {
                icon.name: "search"
                checkable: true
                checked: root.showSearchFilter
                QQC2.ToolTip.text: i18n("Buscar en conversaciones")
                QQC2.ToolTip.visible: hovered
                onClicked: root.showSearchFilter = !root.showSearchFilter
            }

            // Text Size Zoom Toggle (Golden Gate Siri App feature)
            QQC2.ToolButton {
                text: "Aa"
                QQC2.ToolTip.text: i18n("Cambiar tamaño del texto")
                QQC2.ToolTip.visible: hovered
                onClicked: {
                    if (kityClient.textScale < 1.1) {
                        kityClient.textScale = 1.15;
                    } else if (kityClient.textScale < 1.25) {
                        kityClient.textScale = 1.28;
                    } else {
                        kityClient.textScale = 1.0;
                    }
                }
            }

            // Switch to Quick Pill ("Search or Ask") Mode
            QQC2.ToolButton {
                text: "⤡"
                QQC2.ToolTip.text: i18n("Cambiar a Cápsula Rápida (Search or Ask)")
                QQC2.ToolTip.visible: hovered
                onClicked: root.compactMode = true
            }

            // Pin Window On Top (Immediately preceding Preferences)
            QQC2.ToolButton {
                icon.name: root.isPinned ? "window-pin" : "window-unpin"
                checkable: true
                checked: root.isPinned
                QQC2.ToolTip.text: root.isPinned ? i18n("Desanclar de primer plano") : i18n("Mantener siempre encima")
                QQC2.ToolTip.visible: hovered
                onClicked: root.isPinned = !root.isPinned
            }

            // Preferences Button (Far right)
            QQC2.ToolButton {
                icon.name: "configure"
                QQC2.ToolTip.text: i18n("Preferencias de Kity (Golden Gate)")
                QQC2.ToolTip.visible: hovered
                onClicked: root.showPreferences = !root.showPreferences
            }
        }
    ]

    // =========================================================================
    // 3. OVERLAY LAYER: Siri Iridescent Halo Shader
    // =========================================================================
    overlay: [
        ShaderEffect {
            id: siriHaloShader
            anchors.fill: parent
            z: 10
            enabled: false
            visible: opacity > 0.001
            opacity: root.isVoiceActive ? 1.0 : 0.0
            Behavior on opacity {
                NumberAnimation { duration: 250; easing.type: Easing.InOutQuad }
            }

            property real time: root.animTime
            property real audioAmp: kityClient.audioLevel
            property size resolution: Qt.size(width, height)
            property real isSpeaking: kityClient.state === "speaking" ? 1.0 : 0.0
            property real cornerRadius: root.cornerRadius

            fragmentShader: "qrc:/qml/app/shaders/window_glow.frag.qsb"
        }
    ]

    // =========================================================================
    // 4. CLIENT CONTENT AREA
    // =========================================================================
    contentData: [

        // =====================================================================
        // MODEL 1: GOLDEN GATE QUICK OVERLAY (Invoked via Double-Press Meta)
        // Replicates:
        //  - 05_siri_working_pill.png ("Looking into it" + ring of glowing dots)
        //  - 08_spotlight_search_or_ask.png ("Search or Ask" + 4 quick rows)
        //  - 06_siri_spotlight_window.png (Floating vertical answer card with ✕ & ⤢)
        // =====================================================================
        Item {
            id: goldenGateCompactOverlay
            anchors.fill: parent
            visible: root.compactMode

            DragHandler {
                target: null
                onActiveChanged: if (active) root.startSystemMove()
            }

            // -----------------------------------------------------------------
            // STATE 1A: "Looking into it" Working Pill (05_siri_working_pill.png)
            // -----------------------------------------------------------------
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 20
                spacing: 14
                visible: root.compactWorking

                // Ring of 8 glowing white dots of varying sizes
                Item {
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32

                    Repeater {
                        model: 8

                        Rectangle {
                            required property int index
                            readonly property real angle: (index * Math.PI / 4.0) + (root.animTime * 4.2)
                            readonly property real dotSize: 2.5 + ((index % 4) * 1.4)
                            width: dotSize
                            height: dotSize
                            radius: dotSize / 2
                            color: "#FFFFFF"
                            opacity: 0.45 + 0.55 * Math.sin(angle)
                            x: 16 + 10.5 * Math.cos(index * Math.PI / 4.0) - width / 2
                            y: 16 + 10.5 * Math.sin(index * Math.PI / 4.0) - height / 2
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: i18n("Looking into it")
                    color: "#FFFFFF"
                    font.pixelSize: 17
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
            }

            // -----------------------------------------------------------------
            // STATE 1B: "Search or Ask" + Quick Categories (08_spotlight_search_or_ask.png)
            // -----------------------------------------------------------------
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10
                visible: !root.compactWorking && !root.compactAnswerVisible

                // Top Pill: "Search or Ask" + Mic
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    radius: 24
                    color: Qt.rgba(0.06, 0.07, 0.10, 0.92)
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.20)

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 18
                        anchors.rightMargin: 14
                        spacing: 10

                        QQC2.TextField {
                            id: spotlightSearchInput
                            Layout.fillWidth: true
                            focus: root.compactMode && !root.compactAnswerVisible
                            placeholderText: i18n("Search or Ask")
                            placeholderTextColor: Qt.rgba(1, 1, 1, 0.48)
                            color: "#FFFFFF"
                            font.pixelSize: 16
                            background: null
                            onAccepted: {
                                if (text.trim().length > 0) {
                                    root.compactAnswerVisible = true;
                                    kityClient.sendMessage(text.trim(), false);
                                    text = "";
                                }
                            }
                        }

                        // Microphone icon
                        Rectangle {
                            width: 30
                            height: 30
                            radius: 15
                            color: kityClient.isRecording ? "#FF375F" : "transparent"

                            Kirigami.Icon {
                                anchors.centerIn: parent
                                width: 16
                                height: 16
                                source: "audio-input-microphone-symbolic"
                                isMask: true
                                color: Qt.rgba(1, 1, 1, 0.82)
                            }

                            TapHandler {
                                onTapped: kityClient.toggleRecording()
                            }
                        }

                        // Expand to Full Kity App (⤢) button
                        Rectangle {
                            width: 28
                            height: 28
                            radius: 14
                            color: expandPillHover.hovered ? Qt.rgba(1, 1, 1, 0.22) : Qt.rgba(1, 1, 1, 0.12)

                            HoverHandler { id: expandPillHover; cursorShape: Qt.PointingHandCursor }

                            Text {
                                anchors.centerIn: parent
                                text: "⤢"
                                color: "#FFFFFF"
                                font.pixelSize: 13
                                font.bold: true
                            }

                            TapHandler {
                                onTapped: root.compactMode = false
                            }
                        }
                    }
                }

                // Bottom Card: Applications ⌘1, Files ⌘2, Actions ⌘3, Clipboard ⌘4
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 18
                    color: Qt.rgba(0.10, 0.11, 0.15, 0.90)
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.16)

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 2

                        Repeater {
                            model: [
                                { icon: "applications-all-symbolic", label: i18n("Applications"), shortcut: "⌘1", category: "applications" },
                                { icon: "folder-symbolic", label: i18n("Files"), shortcut: "⌘2", category: "files" },
                                { icon: "quickopen", label: i18n("Actions"), shortcut: "⌘3", category: "actions" },
                                { icon: "edit-paste-symbolic", label: i18n("Clipboard"), shortcut: "⌘4", category: "clipboard" }
                            ]

                            Rectangle {
                                id: spotRow
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: 10
                                color: spotRowHover.hovered ? Qt.rgba(1, 1, 1, 0.10) : "transparent"

                                HoverHandler { id: spotRowHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler {
                                    onTapped: {
                                        if (spotRow.modelData.category === "actions" || spotRow.modelData.category === "clipboard") {
                                            root.compactAnswerVisible = true;
                                        }
                                        kityClient.launchSpotlightCategory(spotRow.modelData.category);
                                    }
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 12

                                    Kirigami.Icon {
                                        source: spotRow.modelData.icon
                                        Layout.preferredWidth: 16
                                        Layout.preferredHeight: 16
                                        isMask: true
                                        color: "#FFFFFF"
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: spotRow.modelData.label
                                        color: "#FFFFFF"
                                        font.pixelSize: 14
                                        font.weight: Font.Medium
                                    }

                                    Text {
                                        text: spotRow.modelData.shortcut
                                        color: Qt.rgba(1, 1, 1, 0.42)
                                        font.pixelSize: 13
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // -----------------------------------------------------------------
            // STATE 1C: Spotlight Answer Card (06_siri_spotlight_window.png)
            // -----------------------------------------------------------------
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12
                visible: !root.compactWorking && root.compactAnswerVisible

                // Top Header: Circular ✕ (left), Prompt Pill (center-right), Circular Expand ⤢ (right)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Rectangle {
                        width: 28
                        height: 28
                        radius: 14
                        color: Qt.rgba(1, 1, 1, 0.14)

                        Text {
                            anchors.centerIn: parent
                            text: "✕"
                            color: "#FFFFFF"
                            font.pixelSize: 12
                            font.bold: true
                        }

                        TapHandler {
                            onTapped: {
                                root.compactAnswerVisible = false;
                                root.hide();
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    // Prompt Pill
                    Rectangle {
                        Layout.maximumWidth: 240
                        height: 26
                        width: Math.min(240, spotPromptLabel.implicitWidth + 20)
                        radius: 13
                        color: Qt.rgba(1, 1, 1, 0.08)

                        Text {
                            id: spotPromptLabel
                            anchors.centerIn: parent
                            width: parent.width - 16
                            elide: Text.ElideRight
                            color: Qt.rgba(1, 1, 1, 0.72)
                            font.pixelSize: 12
                            text: {
                                for (let i = kityClient.messages.length - 1; i >= 0; --i) {
                                    if (kityClient.messages[i].role === "user") {
                                        return kityClient.messages[i].content;
                                    }
                                }
                                return i18n("Tell me what's new with Kity...");
                            }
                        }
                    }

                    // Expand ⤢ Button (Transitions to Full Siri App Window!)
                    Rectangle {
                        width: 28
                        height: 28
                        radius: 14
                        color: Qt.rgba(1, 1, 1, 0.14)

                        Text {
                            anchors.centerIn: parent
                            text: "⤢"
                            color: "#FFFFFF"
                            font.pixelSize: 13
                            font.bold: true
                        }

                        TapHandler {
                            onTapped: root.compactMode = false
                        }
                    }
                }

                // Scrollable Answer Content (Strictly vertical, zero horizontal overflow)
                Flickable {
                    id: spotAnswerFlickable
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: width
                    contentHeight: spotAnswerText.implicitHeight + 8
                    flickableDirection: Flickable.VerticalOnly
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true

                    QQC2.ScrollBar.vertical: QQC2.ScrollBar {
                        policy: QQC2.ScrollBar.AsNeeded
                    }

                    TextEdit {
                        id: spotAnswerText
                        width: Math.max(100, spotAnswerFlickable.width - 16)
                        readOnly: true
                        selectByMouse: true
                        cursorVisible: false
                        activeFocusOnPress: false
                        wrapMode: TextEdit.Wrap
                        color: "#FFFFFF"
                        font.pixelSize: 13
                        textFormat: TextEdit.RichText
                        onLinkActivated: link => Qt.openUrlExternally(link)
                        text: {
                            if (kityClient.streamingText.length > 0) {
                                return kityClient.renderMarkdown(kityClient.streamingText);
                            }
                            for (let i = kityClient.messages.length - 1; i >= 0; --i) {
                                if (kityClient.messages[i].role === "assistant") {
                                    return kityClient.renderMarkdown(kityClient.messages[i].content);
                                }
                            }
                            return kityClient.renderMarkdown("### Kity AI\nAsk anything or click **⤢** to open the full Kity conversation app.");
                        }
                    }
                }

                // Bottom Bar: Circular + (left), "Ask Siri/Kity" Pill (center), Circular Mic (right)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Rectangle {
                        width: 30
                        height: 30
                        radius: 15
                        color: Qt.rgba(1, 1, 1, 0.14)

                        Text {
                            anchors.centerIn: parent
                            text: "+"
                            color: "#FFFFFF"
                            font.pixelSize: 16
                        }

                        TapHandler {
                            onTapped: {
                                root.compactMode = false;
                                root.showVisualIntelligence = true;
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 32
                        radius: 16
                        color: Qt.rgba(1, 1, 1, 0.12)
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.18)

                        QQC2.TextField {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            placeholderText: i18n("Ask Kity")
                            placeholderTextColor: Qt.rgba(1, 1, 1, 0.55)
                            color: "#FFFFFF"
                            font.pixelSize: 13
                            background: null
                            onAccepted: {
                                if (text.trim().length > 0) {
                                    kityClient.sendMessage(text.trim(), false);
                                    text = "";
                                }
                            }
                        }
                    }

                    Rectangle {
                        width: 30
                        height: 30
                        radius: 15
                        color: kityClient.isRecording ? "#FF375F" : Qt.rgba(1, 1, 1, 0.14)

                        Kirigami.Icon {
                            anchors.centerIn: parent
                            width: 15
                            height: 15
                            source: "audio-input-microphone-symbolic"
                            isMask: true
                            color: "#FFFFFF"
                        }

                        TapHandler {
                            onTapped: kityClient.toggleRecording()
                        }
                    }
                }
            }
        },

        // =====================================================================
        // MODEL 2: GOLDEN GATE "THE SIRI APP" MAIN CONVERSATION PANE
        // Replicates 09_the_siri_app_window.png
        // =====================================================================
        ColumnLayout {
            id: mainContentColumn
            anchors.fill: parent
            anchors.topMargin: 12
            anchors.leftMargin: CachyTheme.marginWindow
            anchors.rightMargin: CachyTheme.marginWindow
            anchors.bottomMargin: CachyTheme.marginWindow
            spacing: 12
            visible: !root.showPreferences && !root.compactMode

            // -----------------------------------------------------------------
            // Writing Tools Drawer
            // -----------------------------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                visible: root.showWritingTools
                implicitHeight: wtCol.implicitHeight + 16
                radius: CachyTheme.radiusMedium
                color: CachyTheme.cardBackground
                border.width: 1
                border.color: CachyTheme.specularBorder

                ColumnLayout {
                    id: wtCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    QQC2.Label {
                        Layout.fillWidth: true
                        font.bold: true
                        font.pixelSize: 12
                        color: CachyTheme.textPrimary
                        text: i18n("✍️ Writing Tools (Apple Intelligence / Golden Gate)")
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 5
                        columnSpacing: 6
                        rowSpacing: 6

                        Repeater {
                            model: [
                                { label: i18n("Proofread"), action: "proofread" },
                                { label: i18n("Rewrite"), action: "rewrite" },
                                { label: i18n("Friendly"), action: "friendly" },
                                { label: i18n("Professional"), action: "professional" },
                                { label: i18n("Concise"), action: "concise" },
                                { label: i18n("Summary"), action: "summary" },
                                { label: i18n("Key Points"), action: "keypoints" },
                                { label: i18n("Table"), action: "table" },
                                { label: i18n("Español"), action: "translate:es" },
                                { label: i18n("English"), action: "translate:en" }
                            ]

                            CachyButton {
                                required property var modelData
                                Layout.fillWidth: true
                                text: modelData.label
                                onClicked: {
                                    kityClient.applyWritingTool(modelData.action, promptField.text);
                                    root.showWritingTools = false;
                                }
                            }
                        }
                    }
                }
            }

            // -----------------------------------------------------------------
            // Visual Intelligence Drawer
            // -----------------------------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                visible: root.showVisualIntelligence
                implicitHeight: viRow.implicitHeight + 16
                radius: CachyTheme.radiusMedium
                color: CachyTheme.cardBackground
                border.width: 1
                border.color: CachyTheme.specularBorder

                RowLayout {
                    id: viRow
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    QQC2.Label {
                        text: i18n("📷 Visual Intelligence:")
                        font.bold: true
                        color: CachyTheme.textPrimary
                    }

                    QQC2.ComboBox {
                        Layout.preferredWidth: 130
                        model: [
                            { label: i18n("Region"), scope: "region" },
                            { label: i18n("Active Window"), scope: "window" },
                            { label: i18n("Full Screen"), scope: "fullscreen" }
                        ]
                        textRole: "label"
                        onActivated: index => root.visualCaptureScope = model[index].scope
                    }

                    CachyButton {
                        text: i18n("Extract Text (OCR)")
                        onClicked: {
                            kityClient.runVisualIntelligence(root.visualCaptureScope, "ocr");
                            root.showVisualIntelligence = false;
                        }
                    }

                    CachyButton {
                        text: i18n("Describe Screen")
                        primary: true
                        onClicked: {
                            kityClient.runVisualIntelligence(root.visualCaptureScope, "describe");
                            root.showVisualIntelligence = false;
                        }
                    }
                }
            }

            // -----------------------------------------------------------------
            // Math Notes Drawer
            // -----------------------------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                visible: root.showMathNotes
                implicitHeight: mnCol.implicitHeight + 16
                radius: CachyTheme.radiusMedium
                color: CachyTheme.cardBackground
                border.width: 1
                border.color: CachyTheme.specularBorder

                ColumnLayout {
                    id: mnCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        CachyTextField {
                            id: mathInput
                            Layout.fillWidth: true
                            placeholderText: i18n("e.g. price = 250, tax = price * 0.21, total =")
                        }

                        CachyButton {
                            text: i18n("Calculate")
                            primary: true
                            onClicked: {
                                if (mathInput.text.trim().length > 0) {
                                    root.mathResults = kityClient.evaluateMathNotes(mathInput.text);
                                }
                            }
                        }
                    }

                    Repeater {
                        model: root.mathResults
                        QQC2.Label {
                            required property string modelData
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.family: "monospace"
                            color: CachyTheme.textPrimary
                            text: modelData
                        }
                    }
                }
            }

            // -----------------------------------------------------------------
            // Main Document-Style Conversation Area (09_the_siri_app_window.png)
            // -----------------------------------------------------------------
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // Empty State
                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width * 0.9, 440)
                    visible: kityClient.messages.length === 0
                    spacing: 14

                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: i18n("What can I help you with?")
                        font.pixelSize: 22
                        font.weight: Font.Bold
                        color: CachyTheme.textPrimary
                    }

                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: i18n("Press Meta + K anytime for the quick floating pill, or start a conversation below.")
                        font.pixelSize: 13
                        color: CachyTheme.textSecondary
                    }
                }

                // Message List (Clean Golden Gate Document Presentation - strictly vertical)
                ListView {
                    id: chatListView
                    anchors.fill: parent
                    visible: kityClient.messages.length > 0
                    spacing: 16
                    clip: true
                    flickableDirection: Flickable.VerticalOnly
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

                            // Source Citation Pills (Matching "Apple Newsroom +2" / "TechCrunch +2" in 09_the_siri_app_window.png)
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

                            // Subtle Copy & Speak Actions
                            RowLayout {
                                visible: !msgDelegate.isUser
                                spacing: 6

                                QQC2.ToolButton {
                                    icon.name: "edit-copy"
                                    implicitWidth: 26
                                    implicitHeight: 26
                                    onClicked: kityClient.copyToClipboard(msgDelegate.modelData.content)
                                }

                                QQC2.ToolButton {
                                    icon.name: "audio-volume-high"
                                    implicitWidth: 26
                                    implicitHeight: 26
                                    onClicked: kityClient.speakText(msgDelegate.modelData.content)
                                }
                            }
                        }
                    }
                }
            }

            // -----------------------------------------------------------------
            // Bottom Input Bar: Circular [+] | "Ask Siri / Kity" Pill | Circular [🎙]
            // Exact match to 09_the_siri_app_window.png
            // -----------------------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                // Left: Circular [+] Button
                Rectangle {
                    width: 34
                    height: 34
                    radius: 17
                    color: plusHover.hovered ? CachyTheme.cardBackgroundHover : CachyTheme.surfacePill
                    border.width: 1
                    border.color: CachyTheme.specularBorder

                    HoverHandler { id: plusHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: root.showWritingTools = !root.showWritingTools
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "+"
                        font.pixelSize: 18
                        color: CachyTheme.textPrimary
                    }
                }

                // Center: "Ask Kity" Pill Input Field
                Rectangle {
                    Layout.fillWidth: true
                    height: 36
                    radius: 18
                    color: CachyTheme.surfacePill
                    border.width: promptField.activeFocus ? 1.5 : 1
                    border.color: promptField.activeFocus ? CachyTheme.accentColor : CachyTheme.specularBorder

                    QQC2.TextField {
                        id: promptField
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        focus: true
                        color: CachyTheme.textPrimary
                        placeholderTextColor: CachyTheme.textMuted
                        font.pixelSize: 13
                        background: null
                        placeholderText: kityClient.isRecording
                            ? i18n("Listening...")
                            : i18n("Ask Kity")
                        onAccepted: {
                            if (text.trim().length > 0) {
                                kityClient.sendMessage(text.trim(), false);
                                text = "";
                            }
                        }
                    }
                }

                // Right: Circular Microphone Button
                Rectangle {
                    width: 34
                    height: 34
                    radius: 17
                    color: kityClient.isRecording
                        ? "#FF375F"
                        : (micHover.hovered ? CachyTheme.cardBackgroundHover : CachyTheme.surfacePill)
                    border.width: 1
                    border.color: kityClient.isRecording ? "#FF375F" : CachyTheme.specularBorder

                    HoverHandler { id: micHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            if (kityClient.isSpeaking) {
                                kityClient.stopSpeaking();
                            } else if (kityClient.state === "thinking") {
                                kityClient.stopGeneration();
                            } else {
                                kityClient.toggleRecording();
                            }
                        }
                    }

                    Kirigami.Icon {
                        anchors.centerIn: parent
                        width: 16
                        height: 16
                        source: "audio-input-microphone-symbolic"
                        isMask: true
                        color: kityClient.isRecording ? "#FFFFFF" : CachyTheme.textPrimary
                    }
                }
            }
        },

        // =====================================================================
        // 5. GOLDEN GATE SIRI & VOICE PREFERENCES ("Guided by voices")
        // Replicates 10b_siri_voice_window.png & "The Siri app" settings
        // =====================================================================
        Rectangle {
            id: preferencesModal
            anchors.fill: parent
            anchors.margins: CachyTheme.marginWindow
            visible: root.showPreferences && !root.compactMode
            color: "transparent"

            ColumnLayout {
                anchors.fill: parent
                spacing: 14

                // Header Bar with Back "<" Chevron and Title "Voice & Kity Settings"
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    CachyToolbarButton {
                        text: "‹ Back"
                        onClicked: root.showPreferences = false
                    }

                    Text {
                        text: i18n("Kity & Voice — macOS Golden Gate")
                        font.pixelSize: 16
                        font.weight: Font.Bold
                        color: CachyTheme.textPrimary
                    }

                    Item { Layout.fillWidth: true }
                }

                QQC2.ScrollView {
                    id: prefScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff
                    clip: true

                    ColumnLayout {
                        width: prefScroll.availableWidth
                        spacing: 14

                        // =====================================================
                        // CARD 1: "Customize Your Siri Voice" (10b_siri_voice_window.png)
                        // =====================================================
                        CachyCard {
                            Layout.fillWidth: true

                            contentItem: ColumnLayout {
                                spacing: 14

                                // Top Row: Title on Left, Glowing Star/Flower Acoustic Wave Visualizer on Right
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 16

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.alignment: Qt.AlignTop
                                        spacing: 4

                                        Text {
                                            text: i18n("Customize Your Kity Voice")
                                            font.pixelSize: 14
                                            font.weight: Font.Bold
                                            color: CachyTheme.textPrimary
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                            text: i18n("You'll hear this voice in Kity, Maps, and system responses.")
                                            font.pixelSize: 12
                                            color: CachyTheme.textSecondary
                                        }
                                    }

                                    // Dark Rectangular Acoustic Star Visualizer (10b_siri_voice_window.png)
                                    Rectangle {
                                        Layout.preferredWidth: 230
                                        Layout.preferredHeight: 126
                                        radius: 12
                                        color: "#070709"
                                        border.width: 1
                                        border.color: Qt.rgba(1, 1, 1, 0.16)
                                        clip: true

                                        // Concentric acoustic flower/star rings
                                        Repeater {
                                            model: 4

                                            Rectangle {
                                                required property int index
                                                anchors.centerIn: parent
                                                readonly property real waveScale: 1.0 + 0.12 * Math.sin(root.animTime * 3.0 - index * 0.9)
                                                width: (46 + index * 24) * waveScale
                                                height: (46 + index * 24) * waveScale
                                                radius: width * 0.38
                                                rotation: index * 22.5 + root.animTime * 12
                                                color: "transparent"
                                                border.width: 1.4
                                                border.color: Qt.rgba(1, 1, 1, (0.28 - index * 0.05) * (0.5 + kityClient.voiceExpressivity * 0.5))
                                            }
                                        }

                                        // Bright glowing white core orb
                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: 44
                                            height: 44
                                            radius: 22
                                            color: Qt.rgba(1, 1, 1, 0.24)

                                            Rectangle {
                                                anchors.centerIn: parent
                                                width: 28
                                                height: 28
                                                radius: 14
                                                color: "#FFFFFF"
                                            }
                                        }

                                        TapHandler {
                                            onTapped: kityClient.previewSelectedVoice()
                                        }
                                    }
                                }

                                CachyDivider { Layout.fillWidth: true }

                                // Row 1: Accent
                                RowLayout {
                                    Layout.fillWidth: true

                                    Text {
                                        text: i18n("Accent")
                                        font.pixelSize: 13
                                        color: CachyTheme.textPrimary
                                        Layout.fillWidth: true
                                    }

                                    QQC2.ComboBox {
                                        Layout.preferredWidth: 180
                                        model: ["Spanish (Spain)", "Spanish (Latin America)", "American", "British"]
                                        currentIndex: Math.max(0, model.indexOf(kityClient.voiceAccent))
                                        onActivated: index => kityClient.voiceAccent = model[index]
                                    }
                                }

                                CachyDivider { Layout.fillWidth: true }

                                // Row 2: Voice (7 Colored Circular Orbs - 10b_siri_voice_window.png)
                                RowLayout {
                                    Layout.fillWidth: true

                                    Text {
                                        text: i18n("Voice")
                                        font.pixelSize: 13
                                        color: CachyTheme.textPrimary
                                        Layout.fillWidth: true
                                    }

                                    Row {
                                        spacing: 10

                                        Repeater {
                                            model: [
                                                "#30D158", // 1: Green
                                                "#FFD60A", // 2: Yellow
                                                "#FF9F0A", // 3: Orange
                                                "#FF375F", // 4: Red-Pink (Default selected in Golden Gate)
                                                "#BF5AF2", // 5: Magenta
                                                "#5E5CE6", // 6: Indigo
                                                "#64D2FF"  // 7: Blue
                                            ]

                                            Item {
                                                required property string modelData
                                                required property int index
                                                width: 28
                                                height: 28

                                                readonly property bool isSelected: kityClient.voiceIndex === index

                                                // Outer selection ring
                                                Rectangle {
                                                    anchors.centerIn: parent
                                                    width: 28
                                                    height: 28
                                                    radius: 14
                                                    color: "transparent"
                                                    border.width: parent.isSelected ? 2 : 0
                                                    border.color: parent.modelData
                                                }

                                                // Inner colored voice circle
                                                Rectangle {
                                                    anchors.centerIn: parent
                                                    width: 21
                                                    height: 21
                                                    radius: 10.5
                                                    color: parent.modelData

                                                    Text {
                                                        anchors.centerIn: parent
                                                        visible: parent.parent.isSelected
                                                        text: (parent.parent.index + 1).toString()
                                                        color: "#FFFFFF"
                                                        font.pixelSize: 11
                                                        font.bold: true
                                                    }
                                                }

                                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                                TapHandler {
                                                    onTapped: {
                                                        kityClient.voiceIndex = index;
                                                        kityClient.previewSelectedVoice();
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                CachyDivider { Layout.fillWidth: true }

                                // Row 3: Pace Slider (Red/Pink track matching 10b_siri_voice_window.png)
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 16

                                    Text {
                                        text: i18n("Pace")
                                        font.pixelSize: 13
                                        color: CachyTheme.textPrimary
                                        Layout.preferredWidth: 120
                                    }

                                    Item { Layout.fillWidth: true }

                                    QQC2.Slider {
                                        id: paceSlider
                                        Layout.preferredWidth: 240
                                        from: 0.0
                                        to: 1.0
                                        value: kityClient.voicePace
                                        onMoved: kityClient.voicePace = value

                                        background: Rectangle {
                                            x: paceSlider.leftPadding
                                            y: paceSlider.topPadding + paceSlider.availableHeight / 2 - height / 2
                                            width: paceSlider.availableWidth
                                            height: 6
                                            radius: 3
                                            color: CachyTheme.isDark ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(0, 0, 0, 0.14)

                                            Rectangle {
                                                width: paceSlider.visualPosition * parent.width
                                                height: parent.height
                                                radius: 3
                                                color: "#FF375F"
                                            }
                                        }
                                    }
                                }

                                CachyDivider { Layout.fillWidth: true }

                                // Row 4: Expressivity Slider (Red/Pink track matching 10b_siri_voice_window.png)
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 16

                                    Text {
                                        text: i18n("Expressivity")
                                        font.pixelSize: 13
                                        color: CachyTheme.textPrimary
                                        Layout.preferredWidth: 120
                                    }

                                    Item { Layout.fillWidth: true }

                                    QQC2.Slider {
                                        id: exprSlider
                                        Layout.preferredWidth: 240
                                        from: 0.0
                                        to: 1.0
                                        value: kityClient.voiceExpressivity
                                        onMoved: kityClient.voiceExpressivity = value

                                        background: Rectangle {
                                            x: exprSlider.leftPadding
                                            y: exprSlider.topPadding + exprSlider.availableHeight / 2 - height / 2
                                            width: exprSlider.availableWidth
                                            height: 6
                                            radius: 3
                                            color: CachyTheme.isDark ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(0, 0, 0, 0.14)

                                            Rectangle {
                                                width: exprSlider.visualPosition * parent.width
                                                height: parent.height
                                                radius: 3
                                                color: "#FF375F"
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // =====================================================
                        // CARD 2: Conversation History & Shortcut ("The Siri app")
                        // =====================================================
                        CachyCard {
                            Layout.fillWidth: true

                            contentItem: ColumnLayout {
                                spacing: 12

                                Text {
                                    text: i18n("Kity AI, Spotlight & Conversation History")
                                    font.pixelSize: 14
                                    font.weight: Font.Bold
                                    color: CachyTheme.textPrimary
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: i18n("Assistant Experience")
                                        font.pixelSize: 13
                                        color: CachyTheme.textPrimary
                                        Layout.fillWidth: true
                                    }
                                    QQC2.ComboBox {
                                        Layout.preferredWidth: 200
                                        model: ["Kity AI (Golden Gate)", "Kity Classic", "Off"]
                                        currentIndex: kityClient.siriMode === "classic" ? 1 : (kityClient.siriMode === "off" ? 2 : 0)
                                        onActivated: index => kityClient.siriMode = (index === 1 ? "classic" : (index === 2 ? "off" : "ai"))
                                    }
                                }

                                CachyDivider { Layout.fillWidth: true }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: i18n("Quick Pill Keyboard Shortcut")
                                        font.pixelSize: 13
                                        color: CachyTheme.textPrimary
                                        Layout.fillWidth: true
                                    }
                                    Rectangle {
                                        height: 26
                                        width: shortcutBadgeText.implicitWidth + 20
                                        radius: 8
                                        color: CachyTheme.surfacePill
                                        border.width: 1
                                        border.color: CachyTheme.specularBorder
                                        Text {
                                            id: shortcutBadgeText
                                            anchors.centerIn: parent
                                            text: i18n("Meta + K")
                                            font.pixelSize: 12
                                            font.weight: Font.DemiBold
                                            color: CachyTheme.textPrimary
                                        }
                                    }
                                }

                                CachyDivider { Layout.fillWidth: true }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: i18n("Conversation Retention")
                                        font.pixelSize: 13
                                        color: CachyTheme.textPrimary
                                        Layout.fillWidth: true
                                    }
                                    QQC2.ComboBox {
                                        Layout.preferredWidth: 160
                                        model: ["Forever", "1 Year", "30 Days"]
                                        currentIndex: kityClient.historyRetention === "1year" ? 1 : (kityClient.historyRetention === "30days" ? 2 : 0)
                                        onActivated: index => kityClient.historyRetention = (index === 1 ? "1year" : (index === 2 ? "30days" : "forever"))
                                    }
                                }

                                CachyDivider { Layout.fillWidth: true }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: i18n("Visible Lines in Chat Preview")
                                        font.pixelSize: 13
                                        color: CachyTheme.textPrimary
                                        Layout.fillWidth: true
                                    }
                                    QQC2.ComboBox {
                                        Layout.preferredWidth: 160
                                        model: ["2 Lines", "4 Lines", "6 Lines"]
                                        currentIndex: kityClient.previewLines <= 2 ? 0 : (kityClient.previewLines >= 6 ? 2 : 1)
                                        onActivated: index => kityClient.previewLines = (index === 0 ? 2 : (index === 2 ? 6 : 4))
                                    }
                                }

                                CachyDivider { Layout.fillWidth: true }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: i18n("Open Kity App To")
                                        font.pixelSize: 13
                                        color: CachyTheme.textPrimary
                                        Layout.fillWidth: true
                                    }
                                    QQC2.ComboBox {
                                        Layout.preferredWidth: 200
                                        model: ["Past Conversations", "Brand-New Conversation"]
                                        currentIndex: kityClient.openToMode === "new" ? 1 : 0
                                        onActivated: index => kityClient.openToMode = (index === 1 ? "new" : "past")
                                    }
                                }
                            }
                        }

                        // =====================================================
                        // CARD 3: Local & Cloud Intelligence Backend
                        // =====================================================
                        CachyCard {
                            Layout.fillWidth: true

                            contentItem: ColumnLayout {
                                spacing: 12

                                Text {
                                    text: i18n("AFM 3 / Local Ollama & Hands-Free Wake Word")
                                    font.pixelSize: 14
                                    font.weight: Font.Bold
                                    color: CachyTheme.textPrimary
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: i18n("Listen for «¡Hola Kity!»")
                                        font.pixelSize: 13
                                        color: CachyTheme.textPrimary
                                        Layout.fillWidth: true
                                    }
                                    CachySwitch {
                                        checked: kityClient.wakeWordEnabled
                                        onToggled: checked => kityClient.wakeWordEnabled = checked
                                    }
                                }

                                CachyDivider { Layout.fillWidth: true }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: i18n("Active Language Model (LLM)")
                                        font.pixelSize: 13
                                        color: CachyTheme.textPrimary
                                        Layout.fillWidth: true
                                    }
                                    QQC2.ComboBox {
                                        Layout.preferredWidth: 200
                                        model: kityClient.availableModels.length > 0 ? kityClient.availableModels : [kityClient.ollamaModel || "llama3.2:3b"]
                                        onActivated: index => kityClient.setOllamaModel(model[index])
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    ]
}
