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
    sidebarVisible: !root.compactMode
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
    property bool showSearchFilter: false
    property string searchQuery: ""
    property var mathResults: []
    property string visualCaptureScope: "region"
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
            if (root.renamingConversationId !== "") {
                root.renamingConversationId = "";
            } else if (root.deletingConversationId !== "") {
                root.deletingConversationId = "";
            } else if (root.selectedConversationIds.length > 0) {
                root.selectedConversationIds = [];
            } else if (root.compactMode) {
                if (root.compactAnswerVisible) {
                    root.compactAnswerVisible = false;
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
    // 1. GOLDEN GATE EDGE-TO-EDGE SIDEBAR ("The Siri app" - 09_the_siri_app_window.png)
    // =========================================================================
    sidebarHeaderActions: [
        Row {
            spacing: 6
            anchors.verticalCenter: parent.verticalCenter

            // Multi-select Delete Button (Shown when 1+ conversations are Ctrl-selected)
            CachyToolbarButton {
                visible: root.selectedConversationIds.length > 0
                text: "🗑 (" + root.selectedConversationIds.length + ")"
                tooltipText: i18n("Eliminar conversaciones seleccionadas")
                onClicked: {
                    root.deletingConversationId = "__MULTI__";
                }
            }

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
                placeholderText: i18n("Filtrar conversaciones...")
                text: root.searchQuery
                onTextChanged: root.searchQuery = text
            }

            // Delete Confirmation Banner (macOS 27 Golden Gate: "Eliminar" -> "Eliminar")
            Rectangle {
                visible: root.deletingConversationId !== ""
                Layout.fillWidth: true
                implicitHeight: delBannerCol.implicitHeight + 16
                radius: 12
                color: CachyTheme.withAlpha(CachyTheme.dangerColor, 0.16)
                border.width: 1
                border.color: CachyTheme.withAlpha(CachyTheme.dangerColor, 0.45)

                ColumnLayout {
                    id: delBannerCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    QQC2.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        font.bold: true
                        color: CachyTheme.textPrimary
                        text: root.deletingConversationId === "__MULTI__"
                            ? i18n("¿Eliminar definitivamente las %1 conversaciones seleccionadas?", root.selectedConversationIds.length)
                            : i18n("¿Eliminar definitivamente esta conversación?")
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        CachyButton {
                            Layout.fillWidth: true
                            text: i18n("Cancelar")
                            onClicked: root.deletingConversationId = ""
                        }

                        CachyButton {
                            Layout.fillWidth: true
                            destructive: true
                            text: i18n("Eliminar")
                            onClicked: {
                                if (root.deletingConversationId === "__MULTI__") {
                                    kityClient.deleteConversations(root.selectedConversationIds);
                                    root.selectedConversationIds = [];
                                } else {
                                    kityClient.deleteConversation(root.deletingConversationId);
                                }
                                root.deletingConversationId = "";
                            }
                        }
                    }
                }
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
                            readonly property bool isMultiSelected: root.selectedConversationIds.indexOf(modelData.id) !== -1
                            readonly property bool isRenaming: root.renamingConversationId === modelData.id
                            readonly property bool matchesSearch: root.searchQuery.trim().length === 0
                                || (modelData.title && modelData.title.toLowerCase().indexOf(root.searchQuery.toLowerCase()) !== -1)
                                || (modelData.preview && modelData.preview.toLowerCase().indexOf(root.searchQuery.toLowerCase()) !== -1)

                            visible: matchesSearch
                            Layout.fillWidth: true
                            Layout.preferredWidth: kityClient.sidebarViewMode === "grid" ? (sidebarScroll.availableWidth - 10) / 2 : sidebarScroll.availableWidth
                            implicitHeight: convCardCol.implicitHeight + 22
                            radius: 16

                            color: (convCard.isCurrent || convCard.isMultiSelected)
                                ? (CachyTheme.isDark ? Qt.rgba(1, 1, 1, 0.13) : Qt.rgba(1, 1, 1, 0.88))
                                : (convHover.hovered ? CachyTheme.cardBackgroundHover : CachyTheme.cardBackground)

                            border.width: (convCard.isCurrent || convCard.isMultiSelected) ? 2 : 1
                            border.color: convCard.isMultiSelected
                                ? "#FF9F0A"
                                : (convCard.isCurrent ? "#007AFF" : CachyTheme.specularBorder)

                            Behavior on color { ColorAnimation { duration: 120 } }

                            HoverHandler {
                                id: convHover
                                cursorShape: Qt.PointingHandCursor
                            }

                            // Left click or Ctrl+Left click (Multi-selection per Apple doc q7ny3g54v9lk)
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: (eventPoint, button) => {
                                    if (eventPoint.modifiers & Qt.ControlModifier) {
                                        let arr = root.selectedConversationIds.slice();
                                        const pos = arr.indexOf(convCard.modelData.id);
                                        if (pos === -1) {
                                            arr.push(convCard.modelData.id);
                                        } else {
                                            arr.splice(pos, 1);
                                        }
                                        root.selectedConversationIds = arr;
                                    } else {
                                        root.selectedConversationIds = [];
                                        kityClient.selectConversation(convCard.modelData.id);
                                    }
                                }
                            }

                            // Right-click Context Menu (Fijar / Desfijar, Renombrar, Eliminar per Apple doc q7ny3g54v9lk)
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: convContextMenu.popup()
                            }

                            QQC2.Menu {
                                id: convContextMenu

                                QQC2.MenuItem {
                                    icon.name: convCard.modelData.pinned ? "window-unpin" : "window-pin"
                                    text: convCard.modelData.pinned ? i18n("Desfijar") : i18n("Fijar")
                                    onTriggered: kityClient.togglePinConversation(convCard.modelData.id)
                                }

                                QQC2.MenuItem {
                                    icon.name: "edit-rename"
                                    text: i18n("Renombrar…")
                                    onTriggered: {
                                        root.renamingConversationId = convCard.modelData.id;
                                        root.renamingTitleText = convCard.modelData.title || "";
                                    }
                                }

                                QQC2.MenuSeparator {}

                                QQC2.MenuItem {
                                    icon.name: "edit-delete"
                                    text: root.selectedConversationIds.length > 1
                                        ? i18n("Eliminar %1 conversaciones…", root.selectedConversationIds.length)
                                        : i18n("Eliminar…")
                                    onTriggered: {
                                        if (root.selectedConversationIds.length > 1) {
                                            root.deletingConversationId = "__MULTI__";
                                        } else {
                                            root.deletingConversationId = convCard.modelData.id;
                                        }
                                    }
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

                                // Inline Rename Editor when Renombrar is active
                                RowLayout {
                                    visible: convCard.isRenaming
                                    Layout.fillWidth: true
                                    spacing: 4

                                    QQC2.TextField {
                                        id: renameField
                                        Layout.fillWidth: true
                                        text: root.renamingTitleText
                                        font.pixelSize: 12
                                        selectByMouse: true
                                        onAccepted: {
                                            if (text.trim().length > 0) {
                                                kityClient.renameConversation(convCard.modelData.id, text.trim());
                                            }
                                            root.renamingConversationId = "";
                                        }
                                    }

                                    QQC2.ToolButton {
                                        text: "✓"
                                        implicitWidth: 22
                                        implicitHeight: 22
                                        onClicked: {
                                            if (renameField.text.trim().length > 0) {
                                                kityClient.renameConversation(convCard.modelData.id, renameField.text.trim());
                                            }
                                            root.renamingConversationId = "";
                                        }
                                    }
                                }

                                QQC2.Label {
                                    visible: !convCard.isRenaming
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

    // Trailing Actions: Search, Text Size, Pin, and System Settings (kcm_kity)
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

            // Pin Window On Top (Immediately preceding System Settings)
            QQC2.ToolButton {
                icon.name: root.isPinned ? "window-pin" : "window-unpin"
                checkable: true
                checked: root.isPinned
                QQC2.ToolTip.text: root.isPinned ? i18n("Desanclar de primer plano") : i18n("Mantener siempre encima")
                QQC2.ToolTip.visible: hovered
                onClicked: root.isPinned = !root.isPinned
            }

            // System Settings -> Kity (kcm_kity) — Zero duplication!
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
                            onTapped: plusMenu.popup()
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
        // Replicates 09_the_siri_app_window.png & Apple Guide q7ny3g54v9lk
        // =====================================================================
        ColumnLayout {
            id: mainContentColumn
            anchors.fill: parent
            anchors.topMargin: 12
            anchors.leftMargin: CachyTheme.marginWindow
            anchors.rightMargin: CachyTheme.marginWindow
            anchors.bottomMargin: CachyTheme.marginWindow
            spacing: 12
            visible: !root.compactMode

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

                // Left: Circular [+] Button (Apple Guide q7ny3g54v9lk: Fotos, Archivos, Herramientas)
                Rectangle {
                    width: 34
                    height: 34
                    radius: 17
                    color: plusHover.hovered ? CachyTheme.cardBackgroundHover : CachyTheme.surfacePill
                    border.width: 1
                    border.color: CachyTheme.specularBorder

                    HoverHandler { id: plusHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: plusMenu.popup()
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "+"
                        font.pixelSize: 18
                        color: CachyTheme.textPrimary
                    }

                    QQC2.Menu {
                        id: plusMenu

                        QQC2.MenuItem {
                            icon.name: "image-x-generic"
                            text: i18n("Fotos…")
                            onTriggered: photoAttachDialog.open()
                        }

                        QQC2.MenuItem {
                            icon.name: "document-open"
                            text: i18n("Archivos…")
                            onTriggered: fileAttachDialog.open()
                        }

                        QQC2.MenuSeparator {}

                        QQC2.MenuItem {
                            icon.name: "document-edit-symbolic"
                            text: i18n("Herramientas de Escritura")
                            onTriggered: {
                                root.compactMode = false;
                                root.showWritingTools = !root.showWritingTools;
                            }
                        }

                        QQC2.MenuItem {
                            icon.name: "camera-photo-symbolic"
                            text: i18n("Inteligencia Visual (Capturar pantalla)")
                            onTriggered: {
                                root.compactMode = false;
                                root.showVisualIntelligence = !root.showVisualIntelligence;
                            }
                        }

                        QQC2.MenuItem {
                            icon.name: "accessories-calculator"
                            text: i18n("Notas Matemáticas")
                            onTriggered: {
                                root.compactMode = false;
                                root.showMathNotes = !root.showMathNotes;
                            }
                        }
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
        }
    ]
}
