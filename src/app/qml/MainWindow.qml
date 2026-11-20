/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs as QtDialogs

import CachyOS.UI

CachyWindow {
    id: root

    title: i18n("Kity")
    minimumWidth: compactMode ? 280 : 680
    minimumHeight: compactMode ? 48 : 480
    width: compactMode ? (compactWorking ? 310 : (compactAnswerVisible ? 400 : 420)) : 880
    height: compactMode ? (compactWorking ? 62 : (compactAnswerVisible ? 520 : (compactCategoriesVisible ? 272 : 58))) : 620
    color: "transparent"

    showTrafficLights: !root.compactMode
    showTitleBarDivider: !root.compactMode
    showWindowFrame: true
    sidebarVisible: !root.compactMode
    sidebarWidth: 280
    cornerRadius: root.compactMode ? ((root.compactWorking || (!root.compactCategoriesVisible && !root.compactAnswerVisible)) ? 29 : 24) : CachyTheme.radiusWindow
    backgroundColor: root.compactMode ? Qt.rgba(0.07, 0.08, 0.11, 0.90) : "transparent"

    property bool isPinned: false
    property bool compactMode: true
    property bool compactAnswerVisible: false
    property bool compactCategoriesVisible: false
    readonly property bool compactWorking: root.compactMode && (kityClient.state === "thinking" || kityClient.state === "transcribing")

    property int selectedCategoryIndex: -1
    property string liveMathPreview: ""
    readonly property var spotlightCategories: [
        { icon: "applications-all-symbolic", label: i18n("Applications"), shortcut: "⌘1", category: "applications" },
        { icon: "folder-symbolic", label: i18n("Files"), shortcut: "⌘2", category: "files" },
        { icon: "quickopen", label: i18n("Actions"), shortcut: "⌘3", category: "actions" },
        { icon: "edit-paste-symbolic", label: i18n("Clipboard"), shortcut: "⌘4", category: "clipboard" }
    ]

    readonly property var lastAssistantMessage: {
        for (let i = kityClient.messages.length - 1; i >= 0; --i) {
            if (kityClient.messages[i].role === "assistant") {
                return kityClient.messages[i];
            }
        }
        return null;
    }

    Timer {
        id: mathPreviewTimer
        interval: 100
        repeat: false
        onTriggered: {
            let q = spotlightOverlay.spotlightInputText.trim();
            const mathPattern = /^(?:(?:\s*[\d\.,]+\s*[\+\-\*\/\^%]\s*[\d\.,\s\+\-\*\/\^%()=]+)|(?:\s*(?:sqrt|cbrt|sin|cos|tan|log|ln|abs|√|∛)\s*\(?[\d\.,]+\)?\s*=?\s*)|(?:\s*ra[ií]z(?:\s+(?:cuadrada|c[uú]bica))?\s+(?:de\s+)?[\d\.,]+\s*=?\s*)|(?:\s*[\d\.,]+\s*(?:km|m|cm|mm|mi|in|ft|kg|g|lb|oz|l|ml|c|f|celsius|fahrenheit|usd|eur|gbp|jpy|btc|eth|horas?|mins?|minutos?|segs?|segundos?)\s+(?:to|in|a|en)\s+(?:km|m|cm|mm|mi|in|ft|kg|g|lb|oz|l|ml|c|f|celsius|fahrenheit|usd|eur|gbp|jpy|btc|eth|horas?|mins?|minutos?|segs?|segundos?)\s*)|(?:\s*[\d\.,]+\s*%\s*(?:of|de)\s+[\d\.,]+))$/i;

            if (q.length >= 3 && mathPattern.test(q)) {
                let res = kityClient.evaluateMathNotes(q);
                if (res && res.length > 0) {
                    let r = res[0].trim();
                    if (r.length > 0 && r !== q && !r.startsWith("error") && !r.startsWith("warning") && !r.includes("Σ")) {
                        root.liveMathPreview = r;
                        return;
                    }
                }
            }
            root.liveMathPreview = "";
        }
    }

    function restartMathPreviewTimer() {
        mathPreviewTimer.restart();
    }

    property bool showWritingTools: false
    property bool showVisualIntelligence: false
    property bool showMathNotes: false
    property bool showSearchFilter: false
    property string searchQuery: ""
    property var selectedConversationIds: []
    property string renamingConversationId: ""
    property string renamingTitleText: ""
    property string deletingConversationId: ""

    QtDialogs.FileDialog {
        id: photoAttachDialog
        title: i18n("Añadir fotos a la conversación de Kity")
        fileMode: QtDialogs.FileDialog.OpenFiles
        nameFilters: [i18n("Imágenes (*.png *.jpg *.jpeg *.webp *.bmp *.gif)"), i18n("Todos los archivos (*)")]
        onAccepted: {
            let urls = [];
            for (let i = 0; i < selectedFiles.length; ++i) {
                urls.push(selectedFiles[i].toString());
            }
            if (urls.length > 0) {
                if (root.compactMode) {
                    root.compactAnswerVisible = true;
                }
                kityClient.attachFiles(urls);
            }
        }
    }

    QtDialogs.FileDialog {
        id: fileAttachDialog
        title: i18n("Añadir archivos a la conversación de Kity")
        fileMode: QtDialogs.FileDialog.OpenFiles
        nameFilters: [i18n("Todos los archivos (*.*)"), i18n("Documentos y código (*.txt *.md *.json *.csv *.py *.cpp *.h *.qml *.sh)")]
        onAccepted: {
            let urls = [];
            for (let i = 0; i < selectedFiles.length; ++i) {
                urls.push(selectedFiles[i].toString());
            }
            if (urls.length > 0) {
                if (root.compactMode) {
                    root.compactAnswerVisible = true;
                }
                kityClient.attachFiles(urls);
            }
        }
    }

    readonly property bool isVoiceActive: kityClient.state === "listening" || kityClient.state === "thinking" || kityClient.state === "speaking"
    readonly property bool isDark: CachyTheme.isDark

    Timer {
        id: autoDismissTimer
        interval: 120
        repeat: false
        onTriggered: {
            if (!root.active && root.visible && root.compactMode && !root.isPinned) {
                root.hide();
            }
        }
    }

    onActiveChanged: {
        if (!active && root.visible && root.compactMode && !root.isPinned) {
            autoDismissTimer.restart();
        }
    }

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
        root.selectedCategoryIndex = -1;
        root.compactCategoriesVisible = false;
        if (!compactMode) {
            root.compactAnswerVisible = false;
        } else {
            centerSpotlight();
        }
    }
    onVisibleChanged: {
        if (root.compactMode) {
            root.compactAnswerVisible = false;
            root.compactCategoriesVisible = false;
            root.selectedCategoryIndex = -1;
            if (visible) {
                centerSpotlight();
            }
        }
    }

    function centerSpotlight() {
        if (root.compactMode && root.screen) {
            root.x = Math.round(root.screen.virtualX + (root.screen.width - root.width) / 2);
            root.y = Math.round(root.screen.virtualY + root.screen.height * 0.22);
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

    Connections {
        target: kityClient
        function onToggleRequested() {
            if (root.visible && root.active && root.visibility !== Window.Minimized) {
                root.hide();
            } else {
                root.compactMode = true;
                root.compactAnswerVisible = false;
                root.compactCategoriesVisible = false;
                root.selectedCategoryIndex = -1;
                spotlightOverlay.spotlightInputText = "";
                if (root.visibility === Window.Minimized) {
                    root.showNormal();
                } else {
                    root.show();
                }
                root.raise();
                root.requestActivate();
                centerSpotlight();
            }
        }
        function onOpenRequested() {
            root.compactMode = true;
            root.compactAnswerVisible = false;
            root.compactCategoriesVisible = false;
            root.selectedCategoryIndex = -1;
            spotlightOverlay.spotlightInputText = "";
            if (root.visibility === Window.Minimized) {
                root.showNormal();
            } else {
                root.show();
            }
            root.raise();
            root.requestActivate();
            centerSpotlight();
        }
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
        sequences: ["Ctrl+E", "Alt+E"]
        onActivated: root.compactMode = !root.compactMode
    }

    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        enabled: root.compactMode
        onActivated: root.compactMode = false
    }

    Shortcut {
        sequences: ["Ctrl+N"]
        onActivated: kityClient.newConversation()
    }

    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (root.renamingConversationId !== "") {
                root.renamingConversationId = "";
            } else if (root.deletingConversationId !== "") {
                root.deletingConversationId = "";
            } else if (root.selectedConversationIds.length > 0) {
                root.selectedConversationIds = [];
            } else if (root.compactMode) {
                if (root.compactAnswerVisible) {
                    root.compactAnswerVisible = false;
                    root.compactCategoriesVisible = false;
                } else if (root.compactCategoriesVisible) {
                    root.compactCategoriesVisible = false;
                    root.selectedCategoryIndex = -1;
                } else {
                    root.hide();
                }
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
    // 1. GOLDEN GATE EDGE-TO-EDGE SIDEBAR
    // =========================================================================
    sidebarHeaderActions: [
        Row {
            spacing: 6
            anchors.verticalCenter: parent.verticalCenter

            CachyToolbarButton {
                visible: root.selectedConversationIds.length > 0
                text: "🗑 (" + root.selectedConversationIds.length + ")"
                tooltipText: i18n("Eliminar conversaciones seleccionadas")
                onClicked: {
                    root.deletingConversationId = "__MULTI__";
                }
            }

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
        ConversationSidebar {
            id: conversationSidebar
            windowRoot: root
        }
    ]

    // =========================================================================
    // 2. HEADER TOOLBAR
    // =========================================================================
    headerLeading: [
        Row {
            spacing: 6
            anchors.verticalCenter: parent.verticalCenter

            CachyToolbarButton {
                iconName: "document-new-symbolic"
                text: "✎"
                tooltipText: i18n("Nueva conversación (Ctrl+N)")
                onClicked: kityClient.newConversation()
            }

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
                        if (inputBar.text.trim().length === 0) {
                            inputBar.text = kityClient.readWaylandSelection();
                        }
                    }
                }
            }

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

    headerCenter: [
        Rectangle {
            anchors.centerIn: parent
            height: 26
            width: siriPillRow.implicitWidth + 20
            radius: 13
            color: modelPillHover.hovered ? CachyTheme.cardBackgroundHover : CachyTheme.surfacePill
            border.width: 1
            border.color: CachyTheme.specularBorder

            HoverHandler { id: modelPillHover; cursorShape: Qt.PointingHandCursor }
            TapHandler {
                onTapped: modelMenu.popup()
            }

            RowLayout {
                id: siriPillRow
                anchors.centerIn: parent
                spacing: 6

                SiriOrbAnimation {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    assistantState: kityClient.state
                    audioLevel: kityClient.audioLevel
                    customVoiceIndex: kityClient.voiceIndex
                }

                QQC2.Label {
                    text: kityClient.ollamaModel.length > 0 ? ("Kity • " + kityClient.ollamaModel) : i18n("Kity")
                    font.bold: true
                    font.pixelSize: 12
                    color: CachyTheme.textPrimary
                    elide: Text.ElideRight
                    Layout.maximumWidth: 190
                }

                Text {
                    text: "▾"
                    font.pixelSize: 10
                    color: CachyTheme.textSecondary
                }
            }

            QQC2.Menu {
                id: modelMenu

                QQC2.MenuItem {
                    text: i18n("Modelos de Ollama disponibles:")
                    enabled: false
                }

                QQC2.MenuSeparator {}

                Repeater {
                    model: kityClient.availableModels

                    QQC2.MenuItem {
                        required property var modelData
                        text: modelData
                        checkable: true
                        checked: kityClient.ollamaModel === modelData
                        onTriggered: kityClient.setOllamaModel(modelData)
                    }
                }
            }
        }
    ]

    headerActions: [
        Row {
            spacing: 4
            anchors.verticalCenter: parent.verticalCenter

            QQC2.ToolButton {
                icon.name: "search"
                checkable: true
                checked: root.showSearchFilter
                QQC2.ToolTip.text: i18n("Buscar en conversaciones")
                QQC2.ToolTip.visible: hovered
                onClicked: root.showSearchFilter = !root.showSearchFilter
            }

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

            QQC2.ToolButton {
                text: "⤡"
                QQC2.ToolTip.text: i18n("Cambiar a Cápsula Rápida (Search or Ask)")
                QQC2.ToolTip.visible: hovered
                onClicked: root.compactMode = true
            }

            QQC2.ToolButton {
                icon.name: root.isPinned ? "window-pin" : "window-unpin"
                checkable: true
                checked: root.isPinned
                QQC2.ToolTip.text: root.isPinned ? i18n("Desanclar de primer plano") : i18n("Mantener siempre encima")
                QQC2.ToolTip.visible: hovered
                onClicked: root.isPinned = !root.isPinned
            }

            QQC2.ToolButton {
                icon.name: "configure"
                QQC2.ToolTip.text: i18n("Abrir Ajustes de Kity en Preferencias del Sistema…")
                QQC2.ToolTip.visible: hovered
                onClicked: kityClient.openSettings()
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
        SpotlightOverlay {
            id: spotlightOverlay
            visible: root.compactMode
            windowRoot: root
            onStartMoveRequested: root.startSystemMove()
            onExpandToFullRequested: root.compactMode = false
            onOpenPhotoDialogRequested: photoAttachDialog.open()
            onOpenFileDialogRequested: fileAttachDialog.open()
            onOpenWritingToolsRequested: {
                root.compactMode = false;
                root.showWritingTools = true;
            }
            onOpenVisualIntelligenceRequested: {
                root.compactMode = false;
                root.showVisualIntelligence = true;
            }
            onOpenMathNotesRequested: {
                root.compactMode = false;
                root.showMathNotes = true;
            }
        },

        ColumnLayout {
            id: mainContentColumn
            anchors.fill: parent
            anchors.topMargin: 12
            anchors.leftMargin: CachyTheme.marginWindow
            anchors.rightMargin: CachyTheme.marginWindow
            anchors.bottomMargin: CachyTheme.marginWindow
            spacing: 12
            visible: !root.compactMode

            WritingToolsDrawer {
                id: writingToolsDrawer
                visible: root.showWritingTools
                targetText: inputBar.text
                onRequestClose: root.showWritingTools = false
            }

            VisualIntelligenceDrawer {
                id: visualIntelligenceDrawer
                visible: root.showVisualIntelligence
                targetQuery: inputBar.text
                onRequestClose: root.showVisualIntelligence = false
                onRequestHideWindow: root.hide()
                onQueryConsumed: inputBar.text = ""
            }

            MathNotesDrawer {
                id: mathNotesDrawer
                visible: root.showMathNotes
            }

            ConversationView {
                id: conversationView
                onOpenWritingToolsRequested: root.showWritingTools = true
                onOpenMathNotesRequested: root.showMathNotes = true
                onHideWindowRequested: root.hide()
            }

            ConversationInputBar {
                id: inputBar
                onOpenPhotoDialogRequested: photoAttachDialog.open()
                onOpenFileDialogRequested: fileAttachDialog.open()
                onToggleWritingToolsRequested: {
                    root.compactMode = false;
                    root.showWritingTools = !root.showWritingTools;
                }
                onToggleVisualIntelligenceRequested: {
                    root.compactMode = false;
                    root.showVisualIntelligence = !root.showVisualIntelligence;
                }
                onToggleMathNotesRequested: {
                    root.compactMode = false;
                    root.showMathNotes = !root.showMathNotes;
                }
            }
        }
    ]
}
