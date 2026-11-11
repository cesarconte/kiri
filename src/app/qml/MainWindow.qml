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
    width: CachyTheme.windowWidthUtility
    height: 760
    color: "transparent"
    showTrafficLights: !root.compactMode
    cornerRadius: root.compactMode ? Math.round(height / 2) : CachyTheme.radiusWindow

    property bool isPinned: false
    property bool compactMode: false
    property bool showWritingTools: false
    property bool showVisualIntelligence: false
    property bool showMathNotes: false
    property bool showPreferences: false
    property var mathResults: []
    property string visualCaptureScope: "region"

    readonly property bool isVoiceActive: kityClient.state === "listening" || kityClient.state === "thinking" || kityClient.state === "speaking"
    readonly property bool isDark: CachyTheme.isDark

    minimumWidth: compactMode ? 360 : 440
    minimumHeight: compactMode ? 64 : 580

    property real animTime: 0.0
    NumberAnimation on animTime {
        from: 0.0
        to: 1000.0
        duration: 1000000
        loops: Animation.Infinite
        running: root.isVoiceActive
    }

    onIsPinnedChanged: updateWindowFlags()
    onCompactModeChanged: {
        if (compactMode) {
            root.width = 440;
            root.height = 76;
        } else {
            root.width = 540;
            root.height = 760;
        }
        updateWindowFlags();
    }

    function updateWindowFlags() {
        if (compactMode || isPinned) {
            root.flags = Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint;
        } else {
            root.flags = Qt.Window | Qt.FramelessWindowHint;
        }
        root.show();
    }

    Shortcut {
        sequences: ["Ctrl+M", "Alt+M"]
        enabled: kityClient.whisperAvailable
        onActivated: kityClient.toggleRecording()
    }

    Shortcut {
        sequences: ["Ctrl+Shift+W", "Alt+W"]
        onActivated: {
            root.showWritingTools = !root.showWritingTools;
            if (root.showWritingTools) {
                root.showVisualIntelligence = false;
                root.showMathNotes = false;
            }
        }
    }

    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (root.compactMode) {
                if (kityClient.state === "listening") {
                    kityClient.stopRecording();
                } else if (kityClient.isSpeaking) {
                    kityClient.stopSpeaking();
                } else {
                    root.close();
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
    // Header Center: Glowing Iridescent Siri Pill
    // =========================================================================
    headerCenter: [
        Rectangle {
            height: 26
            width: siriPillRow.implicitWidth + 20
            radius: 13
            color: CachyTheme.cardBackground
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

    // =========================================================================
    // Header Actions: Pin & Preferences (Aligned right at 18px margin)
    // Order: Preferences on the far right, preceded by Pin
    // =========================================================================
    headerActions: [
        Row {
            spacing: 4
            anchors.verticalCenter: parent.verticalCenter

            // Pin Window On Top (Antecediendo a Preferencias)
            QQC2.ToolButton {
                icon.name: root.isPinned ? "window-pin" : "window-unpin"
                checkable: true
                checked: root.isPinned
                QQC2.ToolTip.text: root.isPinned ? i18n("Desanclar de primer plano") : i18n("Mantener siempre encima")
                QQC2.ToolTip.visible: hovered
                onClicked: root.isPinned = !root.isPinned
            }

            // Preferences Button (A la derecha del todo)
            QQC2.ToolButton {
                icon.name: "configure"
                QQC2.ToolTip.text: i18n("Preferencias de Kity")
                QQC2.ToolTip.visible: hovered
                onClicked: root.showPreferences = !root.showPreferences
            }
        }
    ]

    // =========================================================================
    // Overlay Layer: Siri Iridescent Halo Shader
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
    // Client Content Area
    // =========================================================================
    contentData: [

        // =====================================================================
        // Siri Floating Dynamic Island Layout (Compact Mode)
        // =====================================================================
        RowLayout {
            id: compactIslandLayout
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            anchors.topMargin: 8
            anchors.bottomMargin: 8
            spacing: 10
            visible: root.compactMode

            // Drag handler so user can grab and reposition the floating island anywhere
            DragHandler {
                target: null
                onActiveChanged: {
                    if (active) root.startSystemMove();
                }
            }

            // Left: Siri Orb Animation (Interactive)
            Item {
                Layout.preferredWidth: 46
                Layout.preferredHeight: 46
                Layout.alignment: Qt.AlignVCenter

                SiriOrbAnimation {
                    anchors.centerIn: parent
                    width: parent.width
                    height: parent.height
                    assistantState: kityClient.state
                    audioLevel: kityClient.audioLevel
                }

                // Click on the orb toggles recording
                HoverHandler { id: orbHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        if (kityClient.state === "listening") {
                            kityClient.stopRecording();
                        } else if (kityClient.isSpeaking) {
                            kityClient.stopSpeaking();
                        } else {
                            kityClient.toggleRecording();
                        }
                    }
                }
                QQC2.ToolTip.text: kityClient.state === "listening" ? i18n("Detener escucha") : i18n("Hablar por voz")
                QQC2.ToolTip.visible: orbHover.hovered
            }

            // Center: Dynamic state and speech ticker
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 1

                HoverHandler { id: centerHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: root.compactMode = false
                }
                QQC2.ToolTip.text: i18n("Clic para expandir a conversación completa")
                QQC2.ToolTip.visible: centerHover.hovered

                RowLayout {
                    spacing: 6

                    // Mini glowing dot for status
                    Rectangle {
                        implicitWidth: 6
                        implicitHeight: 6
                        radius: 3
                        color: {
                            if (kityClient.state === "listening") return "#00e5ff";
                            if (kityClient.state === "thinking") return "#b388ff";
                            if (kityClient.state === "speaking") return "#00e676";
                            return Kirigami.Theme.highlightColor;
                        }
                    }

                    QQC2.Label {
                        text: {
                            if (kityClient.state === "listening") return i18n("Escuchando…");
                            if (kityClient.state === "transcribing") return i18n("Procesando voz…");
                            if (kityClient.state === "thinking") return i18n("Pensando…");
                            if (kityClient.state === "speaking") return i18n("Kity");
                            return i18n("Kity · Asistente");
                        }
                        font.bold: true
                        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                        opacity: 0.95
                    }
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    font.pixelSize: Kirigami.Theme.defaultFont.pixelSize - 1
                    opacity: 0.75
                    text: {
                        if (kityClient.streamingText.length > 0) {
                            return kityClient.streamingText;
                        }
                        if (kityClient.statusMessage.length > 0 && kityClient.state !== "idle") {
                            return kityClient.statusMessage;
                        }
                        if (kityClient.messages.length > 0) {
                            const last = kityClient.messages[kityClient.messages.length - 1];
                            return (last.role === "assistant" ? "" : "Tú: ") + (last.content || "");
                        }
                        return i18n("Toca el orbe para hablar o expandir…");
                    }
                }
            }

            // Right: Actions (Mic toggle, Expand to full chat, Close)
            RowLayout {
                Layout.alignment: Qt.AlignVCenter
                spacing: 2

                // Mic Quick Toggle Button
                QQC2.ToolButton {
                    icon.name: kityClient.isRecording ? "media-playback-stop" : "audio-input-microphone"
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: kityClient.isRecording ? i18n("Detener escucha") : i18n("Hablar por voz")
                    QQC2.ToolTip.visible: hovered
                    onClicked: kityClient.toggleRecording()
                }

                // Expand Button
                QQC2.ToolButton {
                    icon.name: "window-maximize"
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: i18n("Expandir a ventana completa")
                    QQC2.ToolTip.visible: hovered
                    onClicked: root.compactMode = false
                }

                // Close Button
                QQC2.ToolButton {
                    icon.name: "window-close"
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: i18n("Ocultar")
                    QQC2.ToolTip.visible: hovered
                    onClicked: root.close()
                }
            }
        },

        // =====================================================================
        // Window Content Layout
        // =====================================================================
        ColumnLayout {
            id: mainContentColumn
            anchors.fill: parent
            anchors.topMargin: CachyTheme.spacingSmall
            anchors.leftMargin: CachyTheme.marginWindow
            anchors.rightMargin: CachyTheme.marginWindow
            anchors.bottomMargin: CachyTheme.marginWindow
            spacing: CachyTheme.spacingMedium
            visible: !root.showPreferences && !root.compactMode

            // -----------------------------------------------------------------
            // Sub-header Toolbar: Status LED & Quick Action Toggles (From Plasmoid)
            // -----------------------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Rectangle {
                    implicitWidth: 8
                    implicitHeight: 8
                    radius: 4
                    color: {
                        if (!kityClient.ollamaReachable) return Kirigami.Theme.negativeTextColor;
                        if (kityClient.state === "listening") return "#00e5ff";
                        if (kityClient.wakeWordEnabled) return Kirigami.Theme.positiveTextColor;
                        return Kirigami.Theme.disabledTextColor;
                    }
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    font.pixelSize: 11
                    color: CachyTheme.textPrimary
                    opacity: 0.85
                    wrapMode: Text.WordWrap
                    text: {
                        if (!kityClient.ollamaReachable) return i18n("Sin conexión con Ollama");
                        if (kityClient.state === "listening") return i18n("Escuchando…");
                        if (kityClient.state === "transcribing") return i18n("Transcribiendo…");
                        if (kityClient.state === "thinking") return i18n("Pensando…");
                        if (kityClient.state === "speaking") return i18n("Hablando…");
                        if (kityClient.wakeWordEnabled) return i18n("«¡Hola Kity!» activo");
                        return i18n("Listo");
                    }
                }

                // Quick Toggle for "¡Hola Kity!" Wake Word
                QQC2.ToolButton {
                    icon.name: kityClient.wakeWordEnabled ? "mic-on" : "mic-off"
                    checkable: true
                    checked: kityClient.wakeWordEnabled
                    onClicked: kityClient.wakeWordEnabled = !kityClient.wakeWordEnabled
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: kityClient.wakeWordEnabled
                        ? i18n("Escucha activa «¡Hola Kity!» activada (clic para desactivar)")
                        : i18n("Activar escucha manos libres «¡Hola Kity!»")
                }

                // Speech Output Mode Selector Button
                QQC2.ToolButton {
                    icon.name: {
                        const mode = kityClient.speechOutputMode;
                        if (mode === "always") return "audio-volume-high-symbolic";
                        if (mode === "voice_only") return "audio-input-microphone-symbolic";
                        return "audio-volume-muted-symbolic";
                    }
                    onClicked: {
                        const current = kityClient.speechOutputMode;
                        if (current === "manual") {
                            kityClient.setSpeechOutputMode("voice_only");
                        } else if (current === "voice_only") {
                            kityClient.setSpeechOutputMode("always");
                        } else {
                            kityClient.setSpeechOutputMode("manual");
                            kityClient.stopSpeaking();
                        }
                    }
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: {
                        const mode = kityClient.speechOutputMode;
                        if (mode === "always") return i18n("Respuestas por voz: Hablar siempre");
                        if (mode === "voice_only") return i18n("Respuestas por voz: Hablar cuando preguntes por voz");
                        return i18n("Respuestas por voz: Solo manual");
                    }
                }

                // Writing Tools Drawer Toggle
                QQC2.ToolButton {
                    icon.name: "document-edit-symbolic"
                    checkable: true
                    checked: root.showWritingTools
                    onClicked: {
                        root.showWritingTools = !root.showWritingTools;
                        if (root.showWritingTools) {
                            root.showVisualIntelligence = false;
                            root.showMathNotes = false;
                        }
                    }
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: i18n("Herramientas de Escritura")
                }

                // Visual Intelligence Drawer Toggle
                QQC2.ToolButton {
                    icon.name: "camera-photo-symbolic"
                    checkable: true
                    checked: root.showVisualIntelligence
                    onClicked: {
                        root.showVisualIntelligence = !root.showVisualIntelligence;
                        if (root.showVisualIntelligence) {
                            root.showWritingTools = false;
                            root.showMathNotes = false;
                        }
                    }
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: i18n("Inteligencia Visual (Spectacle Wayland + OCR)")
                }

                // Math Notes Drawer Toggle
                QQC2.ToolButton {
                    icon.name: "view-time-schedule-calculus-symbolic"
                    checkable: true
                    checked: root.showMathNotes
                    onClicked: {
                        root.showMathNotes = !root.showMathNotes;
                        if (root.showMathNotes) {
                            root.showWritingTools = false;
                            root.showVisualIntelligence = false;
                            Qt.callLater(() => mathInput.forceActiveFocus());
                        }
                    }
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: i18n("Notas matemáticas (cálculo en vivo con variables)")
                }

                // Kity Mobile Remote Button (Phone as Microphone via Apple Intelligence PWA & KDE Connect)
                QQC2.ToolButton {
                    id: mobileRemoteButton
                    icon.name: "kdeconnect-symbolic"
                    onClicked: {
                        var shared = kityClient.shareMobilePairingWithKdeConnect();
                        if (shared) {
                            kityClient.statusMessage = i18n("Enlace de Kity Remote enviado a tu teléfono por KDE Connect");
                        } else {
                            Qt.openUrlExternally(kityClient.mobilePairingUrl);
                        }
                    }
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: i18n("Conectar teléfono como micrófono (Kity Mobile Portal)")
                }

                // Clear Conversation Button
                QQC2.ToolButton {
                    icon.name: "edit-clear-history-symbolic"
                    visible: kityClient.messages.length > 0
                    onClicked: kityClient.clearHistory()
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: i18n("Limpiar conversación")
                }
            }

            CachyDivider {
                Layout.fillWidth: true
            }

            // -----------------------------------------------------------------
            // Writing Tools Panel Drawer (Phase 2 - 8 Tools)
            // -----------------------------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                visible: root.showWritingTools
                implicitHeight: wtCol.implicitHeight + CachyTheme.spacingSmall * 2
                radius: CachyTheme.radiusMedium
                color: CachyTheme.cardBackground
                border.width: 1
                border.color: CachyTheme.specularBorder

                ColumnLayout {
                    id: wtCol
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Icon {
                            source: "document-edit-symbolic"
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                            color: Kirigami.Theme.highlightColor
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            font.bold: true
                            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                            color: palette.windowText
                            text: promptField.text.trim().length > 0
                                ? i18n("Herramientas de Escritura (usando caja de texto)")
                                : i18n("Herramientas de Escritura (usando selección / portapapeles Wayland)")
                            elide: Text.ElideRight
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 5
                        columnSpacing: Kirigami.Units.smallSpacing
                        rowSpacing: Kirigami.Units.smallSpacing
                        uniformCellWidths: true

                        Repeater {
                            model: [
                                { label: i18n("✍️ Corregir"), action: "proofread" },
                                { label: i18n("📝 Reescribir"), action: "rewrite" },
                                { label: i18n("😊 Amigable"), action: "friendly" },
                                { label: i18n("👔 Profesional"), action: "professional" },
                                { label: i18n("✂️ Conciso"), action: "concise" },
                                { label: i18n("📊 Resumen"), action: "summary" },
                                { label: i18n("📌 Claves"), action: "keypoints" },
                                { label: i18n("📋 Tabla"), action: "table" },
                                { label: i18n("🌐 A español"), action: "translate:es" },
                                { label: i18n("🌐 To English"), action: "translate:en" }
                            ]

                            QQC2.Button {
                                id: wtBtn
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.minimumHeight: Kirigami.Units.gridUnit * 2.2
                                text: modelData.label
                                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                                hoverEnabled: true

                                contentItem: QQC2.Label {
                                    text: wtBtn.text
                                    color: wtBtn.down ? palette.highlight : palette.buttonText
                                    font: wtBtn.font
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }

                                background: Rectangle {
                                    radius: 8
                                    color: wtBtn.down
                                        ? Kirigami.ColorUtils.tintWithAlpha(palette.button, palette.highlight, 0.25)
                                        : (wtBtn.hovered
                                            ? Kirigami.ColorUtils.tintWithAlpha(palette.button, palette.highlight, 0.12)
                                            : palette.button)
                                    border.width: 1
                                    border.color: wtBtn.hovered
                                        ? palette.highlight
                                        : Kirigami.ColorUtils.linearInterpolation(palette.window, palette.windowText, 0.15)

                                    Behavior on color { ColorAnimation { duration: 150 } }
                                    Behavior on border.color { ColorAnimation { duration: 150 } }
                                }

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
            // Visual Intelligence Panel Drawer (Phase 3 - Spectacle + OCR)
            // -----------------------------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                visible: root.showVisualIntelligence
                implicitHeight: viCol.implicitHeight + CachyTheme.spacingSmall * 2
                radius: CachyTheme.radiusMedium
                color: CachyTheme.cardBackground
                border.width: 1
                border.color: CachyTheme.specularBorder

                ColumnLayout {
                    id: viCol
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Icon {
                            source: "camera-photo-symbolic"
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                            color: Kirigami.Theme.highlightColor
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            font.bold: true
                            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                            color: palette.windowText
                            text: i18n("Inteligencia Visual (Spectacle Wayland + Tesseract OCR)")
                            elide: Text.ElideRight
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Label {
                            text: i18n("Capturar:")
                            font: Kirigami.Theme.smallFont
                            color: palette.windowText
                        }

                        QQC2.ComboBox {
                            id: viScopeCombo
                            Layout.preferredWidth: 140
                            model: [
                                { label: i18n("Región"), scope: "region" },
                                { label: i18n("Ventana activa"), scope: "window" },
                                { label: i18n("Pantalla completa"), scope: "fullscreen" }
                            ]
                            textRole: "label"
                            onActivated: index => root.visualCaptureScope = model[index].scope
                        }

                        QQC2.Button {
                            id: ocrBtn
                            text: i18n("📄 Extraer texto (OCR)")
                            icon.name: "document-edit-symbolic"
                            hoverEnabled: true

                            contentItem: RowLayout {
                                spacing: Kirigami.Units.smallSpacing
                                Kirigami.Icon {
                                    source: ocrBtn.icon.name
                                    implicitWidth: Kirigami.Units.iconSizes.small
                                    implicitHeight: Kirigami.Units.iconSizes.small
                                    color: ocrBtn.down ? palette.highlight : palette.buttonText
                                }
                                QQC2.Label {
                                    text: ocrBtn.text
                                    color: ocrBtn.down ? palette.highlight : palette.buttonText
                                    font: ocrBtn.font
                                }
                            }

                            background: Rectangle {
                                radius: 8
                                color: ocrBtn.down
                                    ? Kirigami.ColorUtils.tintWithAlpha(palette.button, palette.highlight, 0.25)
                                    : (ocrBtn.hovered
                                        ? Kirigami.ColorUtils.tintWithAlpha(palette.button, palette.highlight, 0.12)
                                        : palette.button)
                                border.width: 1
                                border.color: ocrBtn.hovered
                                    ? palette.highlight
                                    : Kirigami.ColorUtils.linearInterpolation(palette.window, palette.windowText, 0.15)
                            }

                            onClicked: {
                                kityClient.runVisualIntelligence(root.visualCaptureScope, "ocr");
                                root.showVisualIntelligence = false;
                            }
                        }

                        QQC2.Button {
                            id: descBtn
                            text: i18n("🔍 Describir")
                            icon.name: "image-x-generic-symbolic"
                            hoverEnabled: true

                            contentItem: RowLayout {
                                spacing: Kirigami.Units.smallSpacing
                                Kirigami.Icon {
                                    source: descBtn.icon.name
                                    implicitWidth: Kirigami.Units.iconSizes.small
                                    implicitHeight: Kirigami.Units.iconSizes.small
                                    color: descBtn.down ? palette.highlight : palette.buttonText
                                }
                                QQC2.Label {
                                    text: descBtn.text
                                    color: descBtn.down ? palette.highlight : palette.buttonText
                                    font: descBtn.font
                                }
                            }

                            background: Rectangle {
                                radius: 8
                                color: descBtn.down
                                    ? Kirigami.ColorUtils.tintWithAlpha(palette.button, palette.highlight, 0.25)
                                    : (descBtn.hovered
                                        ? Kirigami.ColorUtils.tintWithAlpha(palette.button, palette.highlight, 0.12)
                                        : palette.button)
                                border.width: 1
                                border.color: descBtn.hovered
                                    ? palette.highlight
                                    : Kirigami.ColorUtils.linearInterpolation(palette.window, palette.windowText, 0.15)
                            }

                            onClicked: {
                                kityClient.runVisualIntelligence(root.visualCaptureScope, "describe");
                                root.showVisualIntelligence = false;
                            }
                        }
                    }
                }
            }

            // -----------------------------------------------------------------
            // Math Notes Panel Drawer (macOS Math Notes live calculator)
            // -----------------------------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                visible: root.showMathNotes
                implicitHeight: mnCol.implicitHeight + CachyTheme.spacingSmall * 2
                radius: CachyTheme.radiusMedium
                color: CachyTheme.cardBackground
                border.width: 1
                border.color: CachyTheme.specularBorder

                ColumnLayout {
                    id: mnCol
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Icon {
                            source: "view-time-schedule-calculus-symbolic"
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                            color: Kirigami.Theme.highlightColor
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            font.bold: true
                            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                            color: palette.windowText
                            text: i18n("Notas matemáticas (escribe operaciones y pulsa =)")
                            elide: Text.ElideRight
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.TextField {
                            id: mathInput
                            Layout.fillWidth: true
                            color: palette.text
                            placeholderTextColor: palette.placeholderText
                            selectionColor: palette.highlight
                            selectedTextColor: palette.highlightedText
                            font: Kirigami.Theme.defaultFont
                            placeholderText: i18n("Ejemplo: precio = 250 €, iva = precio * 0.21, total =")
                            background: Rectangle {
                                radius: 8
                                color: palette.base
                                border.width: mathInput.activeFocus ? 1.5 : 1
                                border.color: mathInput.activeFocus
                                    ? palette.highlight
                                    : Kirigami.ColorUtils.linearInterpolation(palette.base, palette.text, 0.18)
                            }
                            onAccepted: {
                                if (text.trim().length > 0) {
                                    root.mathResults = kityClient.evaluateMathNotes(text);
                                }
                            }
                        }

                        QQC2.Button {
                            id: calcBtn
                            text: i18n("Calcular")
                            icon.name: "checkmark"
                            hoverEnabled: true

                            contentItem: RowLayout {
                                spacing: Kirigami.Units.smallSpacing
                                Kirigami.Icon {
                                    source: calcBtn.icon.name
                                    implicitWidth: Kirigami.Units.iconSizes.small
                                    implicitHeight: Kirigami.Units.iconSizes.small
                                    color: calcBtn.down ? palette.highlight : palette.buttonText
                                }
                                QQC2.Label {
                                    text: calcBtn.text
                                    color: calcBtn.down ? palette.highlight : palette.buttonText
                                    font: calcBtn.font
                                }
                            }

                            background: Rectangle {
                                radius: 8
                                color: calcBtn.down
                                    ? Kirigami.ColorUtils.tintWithAlpha(palette.button, palette.highlight, 0.25)
                                    : (calcBtn.hovered
                                        ? Kirigami.ColorUtils.tintWithAlpha(palette.button, palette.highlight, 0.12)
                                        : palette.button)
                                border.width: 1
                                border.color: calcBtn.hovered
                                    ? palette.highlight
                                    : Kirigami.ColorUtils.linearInterpolation(palette.window, palette.windowText, 0.15)
                            }

                            onClicked: {
                                if (mathInput.text.trim().length > 0) {
                                    root.mathResults = kityClient.evaluateMathNotes(mathInput.text);
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: root.mathResults.length > 0
                        spacing: 2

                        Repeater {
                            model: root.mathResults
                            delegate: QQC2.Label {
                                required property string modelData
                                Layout.fillWidth: true
                                font.family: "monospace"
                                color: palette.text
                                text: modelData
                            }
                        }
                    }
                }
            }

            // -----------------------------------------------------------------
            // Central Chat & Welcome Area (From Plasmoid)
            // -----------------------------------------------------------------
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // Welcome Screen when conversation is empty
                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width * 0.94, 460)
                    visible: kityClient.messages.length === 0
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
                        wrapMode: Text.WordWrap
                        text: i18n("Pulsa la esfera inferior o di «¡Hola Kity!» para hablar, o elige una acción rápida:")
                        font: Kirigami.Theme.defaultFont
                        color: CachyTheme.textSecondary
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: Kirigami.Units.smallSpacing + 2
                        rowSpacing: Kirigami.Units.smallSpacing + 2
                        uniformCellWidths: true

                        Repeater {
                            model: [
                                { label: "🖥️ Analizar pantalla", prompt: "Analiza qué hay en mi pantalla", isWritingTool: false },
                                { label: "✍️ Herramientas de escritura", prompt: "", isWritingTool: true },
                                { label: "📋 Resumir copiado", prompt: "Resume lo que he copiado en el portapapeles", isWritingTool: false },
                                { label: "⚡ Estado del sistema", prompt: "Estado del sistema", isWritingTool: false },
                                { label: "🎵 ¿Qué canción suena?", prompt: "¿Qué canción está sonando?", isWritingTool: false },
                                { label: "🌙 Modo oscuro", prompt: "Activa el modo oscuro", isWritingTool: false }
                            ]

                            QQC2.Button {
                                id: actionBtn
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
                                        text: actionBtn.text
                                        color: actionBtn.down ? palette.highlight : palette.buttonText
                                        font: actionBtn.font
                                        elide: Text.ElideRight
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }

                                background: Rectangle {
                                    radius: CachyTheme.radiusSmall
                                    color: actionBtn.down
                                        ? CachyTheme.cardBackgroundHover
                                        : (actionBtn.hovered
                                            ? CachyTheme.cardBackgroundHover
                                            : CachyTheme.cardBackground)
                                    border.width: 1
                                    border.color: CachyTheme.specularBorder

                                    Behavior on color { ColorAnimation { duration: 150 } }
                                    Behavior on border.color { ColorAnimation { duration: 150 } }
                                }

                                onClicked: {
                                    if (modelData.isWritingTool) {
                                        root.showWritingTools = true;
                                    } else {
                                        kityClient.sendMessage(modelData.prompt, false);
                                    }
                                }
                            }
                        }
                    }
                }

                // Chat Message List
                QQC2.ScrollView {
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    visible: kityClient.messages.length > 0
                    clip: true

                    ListView {
                        id: chatListView
                        anchors.fill: parent
                        spacing: Kirigami.Units.mediumSpacing
                        model: kityClient.messages

                        onCountChanged: {
                            Qt.callLater(() => chatListView.positionViewAtEnd());
                        }

                        Connections {
                            target: kityClient
                            function onStreamingTextChanged() {
                                chatListView.positionViewAtEnd();
                            }
                        }

                        footer: Item {
                            width: chatListView.width
                            visible: kityClient.streamingText.length > 0
                            height: visible ? streamBubble.height + Kirigami.Units.mediumSpacing : 0

                            Rectangle {
                                id: streamBubble
                                width: parent.width * 0.92
                                height: streamText.implicitHeight + CachyTheme.spacingMedium * 2
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.topMargin: CachyTheme.spacingSmall
                                radius: CachyTheme.radiusMedium
                                color: CachyTheme.cardBackground
                                border.width: 1
                                border.color: CachyTheme.specularBorder

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
                                    text: kityClient.renderMarkdown(kityClient.streamingText)
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
                                ? kityClient.extractTerminalCommands(modelData.content)
                                : []

                            Rectangle {
                                id: bubbleRect
                                width: msgDelegate.isUser
                                    ? Math.min(parent.width * 0.88, Math.max(120, msgColumn.implicitWidth + CachyTheme.spacingMedium * 2))
                                    : parent.width * 0.92
                                height: msgColumn.implicitHeight + CachyTheme.spacingMedium * 2
                                anchors.right: msgDelegate.isUser ? parent.right : undefined
                                anchors.left: msgDelegate.isUser ? undefined : parent.left
                                radius: CachyTheme.radiusMedium
                                color: msgDelegate.isUser
                                    ? CachyTheme.withAlpha(CachyTheme.accentColor, 0.22)
                                    : CachyTheme.cardBackground
                                border.width: 1
                                border.color: msgDelegate.isUser
                                    ? CachyTheme.accentColor
                                    : CachyTheme.specularBorder

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
                                            : kityClient.renderMarkdown(msgDelegate.modelData.content)

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
                                            delegate: RowLayout {
                                                required property string modelData
                                                Layout.fillWidth: true
                                                spacing: Kirigami.Units.smallSpacing

                                                QQC2.Label {
                                                    Layout.fillWidth: true
                                                    font.family: "monospace"
                                                    color: palette.text
                                                    text: modelData
                                                    elide: Text.ElideRight
                                                }

                                                QQC2.Button {
                                                    icon.name: "system-run"
                                                    text: i18n("Ejecutar en Konsole")
                                                    onClicked: kityClient.executeCommandInKonsole(modelData)
                                                }
                                            }
                                        }
                                    }

                                    // Generative Interactive Cards (Phase 5 - UI Generativa)
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

                                    // Interactive Source Chips (Phase 3 - Deep Web Research & Citations)
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
                                        visible: !msgDelegate.isUser
                                        Layout.fillWidth: true
                                        spacing: Kirigami.Units.smallSpacing

                                        QQC2.ToolButton {
                                            icon.name: "edit-copy"
                                            QQC2.ToolTip.visible: hovered
                                            QQC2.ToolTip.text: i18n("Copiar al portapapeles")
                                            onClicked: kityClient.copyToClipboard(msgDelegate.modelData.content)
                                        }

                                        QQC2.ToolButton {
                                            icon.name: "audio-volume-high"
                                            QQC2.ToolTip.visible: hovered
                                            QQC2.ToolTip.text: i18n("Escuchar respuesta con voz")
                                            onClicked: kityClient.speakText(msgDelegate.modelData.content)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Dynamic status bar when recording, transcribing, thinking, or speaking (collapses when empty)
            QQC2.Label {
                Layout.fillWidth: true
                visible: kityClient.statusMessage.length > 0 && kityClient.state !== "idle"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: kityClient.statusMessage
                font: Kirigami.Theme.smallFont
                color: kityClient.state === "error" ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.highlightColor
                wrapMode: Text.WordWrap
            }

            // -----------------------------------------------------------------
            // Bottom Dock: Dynamic Animated Siri Orb & Text Input (From Plasmoid)
            // -----------------------------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: dockCol.implicitHeight + Kirigami.Units.mediumSpacing * 2
                radius: Kirigami.Units.smallSpacing * 1.5
                color: (kityClient.state === "listening")
                    ? Qt.rgba(CachyTheme.accentColor.r, CachyTheme.accentColor.g, CachyTheme.accentColor.b, 0.14)
                    : CachyTheme.cardBackground
                border.width: 1
                border.color: (kityClient.state === "listening")
                    ? CachyTheme.accentColor
                    : CachyTheme.specularBorder

                ColumnLayout {
                    id: dockCol
                    anchors.fill: parent
                    anchors.topMargin: Kirigami.Units.mediumSpacing
                    anchors.bottomMargin: Kirigami.Units.mediumSpacing
                    anchors.leftMargin: Kirigami.Units.mediumSpacing
                    anchors.rightMargin: Kirigami.Units.mediumSpacing
                    spacing: Kirigami.Units.mediumSpacing

                    // Siri Orb Centered Interactive Voice Control (Dynamic Audio Reactivity)
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: siriOrb.implicitHeight + Kirigami.Units.smallSpacing * 2
                        clip: false

                        SiriOrbAnimation {
                            id: siriOrb
                            anchors.centerIn: parent
                            assistantState: kityClient.state
                            audioLevel: kityClient.audioLevel
                            onClicked: {
                                if (kityClient.isSpeaking) {
                                    kityClient.stopSpeaking();
                                } else if (kityClient.state === "thinking") {
                                    kityClient.stopGeneration();
                                } else {
                                    kityClient.toggleRecording();
                                }
                            }
                        }
                    }

                    // Bottom Text Input & Send Bar (Cajón de escribir nativo estilo Liquid Glass)
                    Rectangle {
                        id: promptInputBox
                        Layout.fillWidth: true
                        implicitHeight: Math.max(promptInputRow.implicitHeight + CachyTheme.spacingSmall * 2, 44)
                        radius: CachyTheme.radiusMedium
                        color: CachyTheme.cardBackground
                        border.width: promptField.activeFocus ? 1.5 : 1
                        border.color: promptField.activeFocus
                            ? CachyTheme.accentColor
                            : CachyTheme.specularBorder

                        Behavior on border.color { ColorAnimation { duration: 150 } }

                        RowLayout {
                            id: promptInputRow
                            anchors.fill: parent
                            anchors.leftMargin: Kirigami.Units.mediumSpacing
                            anchors.rightMargin: Kirigami.Units.smallSpacing
                            anchors.topMargin: Kirigami.Units.smallSpacing
                            anchors.bottomMargin: Kirigami.Units.smallSpacing
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.ScrollView {
                                id: promptScrollView
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                Layout.maximumHeight: Kirigami.Units.gridUnit * 6
                                QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

                                QQC2.TextArea {
                                    id: promptField
                                    focus: true
                                    color: palette.text
                                    placeholderTextColor: palette.placeholderText
                                    selectionColor: palette.highlight
                                    selectedTextColor: palette.highlightedText
                                    font: Kirigami.Theme.defaultFont
                                    background: null
                                    placeholderText: kityClient.isRecording
                                        ? i18n("Escuchando tu voz…")
                                        : i18n("Pregúntale o pídele cualquier cosa a Kity…")
                                    enabled: !kityClient.isRecording
                                    wrapMode: TextEdit.Wrap

                                    Keys.onPressed: event => {
                                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ShiftModifier)) {
                                            if (kityClient.state !== "thinking" && text.trim().length > 0) {
                                                kityClient.sendMessage(text, false);
                                                text = "";
                                            }
                                            event.accepted = true;
                                        }
                                    }
                                }
                            }

                            QQC2.Button {
                                id: sendBtn
                                Layout.alignment: Qt.AlignBottom
                                Layout.preferredWidth: Kirigami.Units.gridUnit * 2.2
                                Layout.preferredHeight: Kirigami.Units.gridUnit * 2.2
                                hoverEnabled: true
                                enabled: kityClient.state === "thinking" || promptField.text.trim().length > 0

                                contentItem: Kirigami.Icon {
                                    anchors.centerIn: parent
                                    source: kityClient.state === "thinking" ? "process-stop-symbolic" : "document-send-symbolic"
                                    color: sendBtn.enabled
                                        ? (kityClient.state === "thinking" ? Kirigami.Theme.negativeTextColor : (sendBtn.hovered ? palette.highlight : palette.buttonText))
                                        : palette.placeholderText
                                }

                                background: Rectangle {
                                    radius: 8
                                    color: sendBtn.down
                                        ? Kirigami.ColorUtils.tintWithAlpha(palette.button, palette.highlight, 0.25)
                                        : (sendBtn.hovered
                                            ? Kirigami.ColorUtils.tintWithAlpha(palette.button, palette.highlight, 0.12)
                                            : (sendBtn.enabled ? palette.button : "transparent"))
                                    border.width: 1
                                    border.color: sendBtn.enabled
                                        ? (sendBtn.hovered ? palette.highlight : Kirigami.ColorUtils.linearInterpolation(palette.window, palette.windowText, 0.15))
                                        : "transparent"
                                }

                                onClicked: {
                                    if (kityClient.state === "thinking") {
                                        kityClient.stopGeneration();
                                    } else if (promptField.text.trim().length > 0) {
                                        kityClient.sendMessage(promptField.text, false);
                                        promptField.text = "";
                                    }
                                }
                                QQC2.ToolTip.visible: hovered
                                QQC2.ToolTip.text: kityClient.state === "thinking" ? i18n("Detener generación") : i18n("Enviar mensaje")
                            }
                        }
                    }
                }
            }
        },

        // =====================================================================
        // Redesigned Preferences Modal (Kirigami Liquid Glass Sheet)
        // =====================================================================
        Rectangle {
            id: preferencesModal
            anchors.fill: parent
            anchors.margins: 4
            radius: CachyTheme.radiusMedium
            visible: root.showPreferences
            color: root.isDark ? Qt.rgba(0.12, 0.12, 0.15, 0.95) : Qt.rgba(0.96, 0.97, 1.0, 0.95)
            border.width: 1
            border.color: CachyTheme.specularBorder

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Kirigami.Units.largeSpacing + 4
                spacing: Kirigami.Units.largeSpacing

                // Header
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        source: "configure"
                        implicitWidth: Kirigami.Units.iconSizes.medium
                        implicitHeight: Kirigami.Units.iconSizes.medium
                        color: Kirigami.Theme.highlightColor
                    }

                    Kirigami.Heading {
                        text: i18n("Preferencias de Kity")
                        level: 2
                    }

                    Item { Layout.fillWidth: true }

                    QQC2.ToolButton {
                        icon.name: "window-close"
                        onClicked: root.showPreferences = false
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.text: i18n("Cerrar preferencias")
                    }
                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                // Scrollable Settings Cards Container using native KDE Kirigami.AbstractCard
                QQC2.ScrollView {
                    id: prefScrollView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff
                    clip: true

                    ColumnLayout {
                        width: prefScrollView.availableWidth
                        spacing: Kirigami.Units.largeSpacing

                        // =====================================================
                        // Group 1: Voz y Activación Manos Libres
                        // =====================================================
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.Label {
                                text: i18n("🎙️ Voz y Activación Manos Libres")
                                font.bold: true
                                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize
                                color: Kirigami.Theme.highlightColor
                            }

                            Kirigami.AbstractCard {
                                Layout.fillWidth: true

                                contentItem: ColumnLayout {
                                    spacing: Kirigami.Units.mediumSpacing

                                    // Row 1: Wake word
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumHeight: 38
                                        spacing: Kirigami.Units.mediumSpacing
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            Layout.preferredWidth: 0
                                            spacing: 2
                                            QQC2.Label {
                                                text: i18n("Activación por voz «¡Hola Kity!»")
                                                font.bold: true
                                                Layout.fillWidth: true
                                            }
                                            QQC2.Label {
                                                text: i18n("Escuchar en segundo plano sin usar el ratón ni el teclado")
                                                font: Kirigami.Theme.smallFont
                                                opacity: 0.75
                                                wrapMode: Text.WordWrap
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 0
                                            }
                                        }
                                        QQC2.Switch {
                                            checked: kityClient.wakeWordEnabled
                                            onToggled: kityClient.wakeWordEnabled = checked
                                        }
                                    }

                                    Kirigami.Separator { Layout.fillWidth: true }

                                    // Row 2: Speech output mode
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumHeight: 38
                                        spacing: Kirigami.Units.mediumSpacing
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            Layout.preferredWidth: 0
                                            spacing: 2
                                            QQC2.Label {
                                                text: i18n("Respuestas habladas")
                                                font.bold: true
                                                Layout.fillWidth: true
                                            }
                                            QQC2.Label {
                                                text: i18n("Modo de lectura en voz alta de las respuestas")
                                                font: Kirigami.Theme.smallFont
                                                opacity: 0.75
                                                wrapMode: Text.WordWrap
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 0
                                            }
                                        }
                                        QQC2.ComboBox {
                                            Layout.preferredWidth: 200
                                            model: [
                                                { value: "always", text: i18n("Hablar siempre") },
                                                { value: "voice_only", text: i18n("Solo si preguntas por voz") },
                                                { value: "manual", text: i18n("Solo manual") }
                                            ]
                                            textRole: "text"
                                            valueRole: "value"
                                            currentIndex: {
                                                if (kityClient.speechOutputMode === "always") return 0;
                                                if (kityClient.speechOutputMode === "voice_only") return 1;
                                                return 2;
                                            }
                                            onActivated: index => {
                                                kityClient.setSpeechOutputMode(model[index].value);
                                            }
                                        }
                                    }

                                    Kirigami.Separator { Layout.fillWidth: true }

                                    // Row 3: Earcons
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumHeight: 38
                                        spacing: Kirigami.Units.mediumSpacing
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            Layout.preferredWidth: 0
                                            spacing: 2
                                            QQC2.Label {
                                                text: i18n("Avisos acústicos (Estilo Siri)")
                                                font.bold: true
                                                Layout.fillWidth: true
                                            }
                                            QQC2.Label {
                                                text: i18n("Tono armónico sutil al activar o detener el micrófono")
                                                font: Kirigami.Theme.smallFont
                                                opacity: 0.75
                                                wrapMode: Text.WordWrap
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 0
                                            }
                                        }
                                        QQC2.Switch {
                                            checked: kityClient.earconsEnabled
                                            onToggled: kityClient.earconsEnabled = checked
                                        }
                                    }

                                    Kirigami.Separator { Layout.fillWidth: true }

                                    // Row 4: VAD Silence Detection
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumHeight: 38
                                        spacing: Kirigami.Units.mediumSpacing
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            Layout.preferredWidth: 0
                                            spacing: 2
                                            QQC2.Label {
                                                text: i18n("Detección de fin de frase (VAD)")
                                                font.bold: true
                                                Layout.fillWidth: true
                                            }
                                            QQC2.Label {
                                                text: i18n("Detener la grabación automáticamente cuando termines de hablar")
                                                font: Kirigami.Theme.smallFont
                                                opacity: 0.75
                                                wrapMode: Text.WordWrap
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 0
                                            }
                                        }
                                        QQC2.Switch {
                                            checked: kityClient.vadEnabled
                                            onToggled: kityClient.vadEnabled = checked
                                        }
                                    }

                                    Kirigami.Separator { Layout.fillWidth: true }

                                    // Row 5: Local Engines Status
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumHeight: 38
                                        spacing: Kirigami.Units.mediumSpacing
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            Layout.preferredWidth: 0
                                            spacing: 2
                                            QQC2.Label {
                                                text: i18n("Motores de voz locales")
                                                font.bold: true
                                                Layout.fillWidth: true
                                            }
                                            QQC2.Label {
                                                text: i18n("Whisper.cpp (STT) y Piper TTS (síntesis neuronal)")
                                                font: Kirigami.Theme.smallFont
                                                opacity: 0.75
                                                wrapMode: Text.WordWrap
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 0
                                            }
                                        }
                                        RowLayout {
                                            Layout.alignment: Qt.AlignVCenter
                                            spacing: Kirigami.Units.smallSpacing
                                            Rectangle {
                                                implicitWidth: whisperBadgeText.implicitWidth + 14
                                                implicitHeight: 24
                                                radius: 12
                                                color: kityClient.whisperAvailable ? Qt.rgba(0.18, 0.75, 0.35, 0.15) : Qt.rgba(0.9, 0.2, 0.2, 0.15)
                                                border.width: 1
                                                border.color: kityClient.whisperAvailable ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor
                                                QQC2.Label {
                                                    id: whisperBadgeText
                                                    anchors.centerIn: parent
                                                    text: kityClient.whisperAvailable ? i18n("✓ Whisper") : i18n("✗ Whisper")
                                                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                                                    font.bold: true
                                                    color: kityClient.whisperAvailable ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor
                                                }
                                            }
                                            Rectangle {
                                                implicitWidth: piperBadgeText.implicitWidth + 14
                                                implicitHeight: 24
                                                radius: 12
                                                color: kityClient.piperAvailable ? Qt.rgba(0.18, 0.75, 0.35, 0.15) : Qt.rgba(0.9, 0.2, 0.2, 0.15)
                                                border.width: 1
                                                border.color: kityClient.piperAvailable ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor
                                                QQC2.Label {
                                                    id: piperBadgeText
                                                    anchors.centerIn: parent
                                                    text: kityClient.piperAvailable ? i18n("✓ Piper TTS") : i18n("✗ Piper TTS")
                                                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                                                    font.bold: true
                                                    color: kityClient.piperAvailable ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // =====================================================
                        // Group 2: Inteligencia Artificial Local (Ollama)
                        // =====================================================
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.Label {
                                text: i18n("🧠 Inteligencia Artificial Local (Ollama)")
                                font.bold: true
                                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize
                                color: Kirigami.Theme.highlightColor
                            }

                            Kirigami.AbstractCard {
                                Layout.fillWidth: true

                                contentItem: ColumnLayout {
                                    spacing: Kirigami.Units.mediumSpacing

                                    // Row 1: Connection status
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumHeight: 38
                                        spacing: Kirigami.Units.mediumSpacing
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            Layout.preferredWidth: 0
                                            spacing: 2
                                            QQC2.Label {
                                                text: i18n("Servidor Ollama")
                                                font.bold: true
                                                Layout.fillWidth: true
                                            }
                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 6
                                                Rectangle {
                                                    implicitWidth: 8
                                                    implicitHeight: 8
                                                    radius: 4
                                                    color: kityClient.ollamaReachable ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor
                                                }
                                                QQC2.Label {
                                                    text: kityClient.ollamaReachable
                                                        ? i18n("Conectado en localhost:11434")
                                                        : i18n("No detectado en localhost:11434")
                                                    font: Kirigami.Theme.smallFont
                                                    color: kityClient.ollamaReachable ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor
                                                    wrapMode: Text.WordWrap
                                                    Layout.fillWidth: true
                                                    Layout.minimumWidth: 0
                                                }
                                            }
                                        }
                                        QQC2.Button {
                                            id: checkBtn
                                            Layout.alignment: Qt.AlignVCenter
                                            property bool isChecking: false

                                            contentItem: RowLayout {
                                                spacing: Kirigami.Units.smallSpacing
                                                Kirigami.Icon {
                                                    id: refreshIcon
                                                    source: "view-refresh"
                                                    implicitWidth: Kirigami.Units.iconSizes.small
                                                    implicitHeight: Kirigami.Units.iconSizes.small
                                                    Layout.alignment: Qt.AlignVCenter
                                                    transformOrigin: Item.Center

                                                    RotationAnimation {
                                                        target: refreshIcon
                                                        from: 0
                                                        to: 360
                                                        duration: 650
                                                        loops: Animation.Infinite
                                                        running: checkBtn.isChecking
                                                    }
                                                }
                                                QQC2.Label {
                                                    text: checkBtn.isChecking ? i18n("Comprobando…") : i18n("Comprobar")
                                                    Layout.alignment: Qt.AlignVCenter
                                                }
                                            }

                                            onClicked: {
                                                isChecking = true;
                                                checkTimer.restart();
                                                kityClient.checkReadiness();
                                                kityClient.refreshModels();
                                            }

                                            Timer {
                                                id: checkTimer
                                                interval: 850
                                                onTriggered: checkBtn.isChecking = false
                                            }
                                        }
                                    }

                                    Kirigami.Separator { Layout.fillWidth: true }

                                    // Row 2: Active Ollama model selector
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumHeight: 38
                                        spacing: Kirigami.Units.mediumSpacing
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            Layout.preferredWidth: 0
                                            spacing: 2
                                            QQC2.Label {
                                                text: i18n("Modelo activo de lenguaje (LLM)")
                                                font.bold: true
                                                Layout.fillWidth: true
                                            }
                                            QQC2.Label {
                                                text: kityClient.availableModels.length > 0
                                                    ? i18n("Modelo local de Ollama que procesará tus preguntas y tareas")
                                                    : i18n("Inicia Ollama y pulsa «Comprobar» para detectar tus modelos")
                                                font: Kirigami.Theme.smallFont
                                                opacity: 0.75
                                                wrapMode: Text.WordWrap
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 0
                                            }
                                        }

                                        QQC2.ComboBox {
                                            id: modelSelectorCombo
                                            Layout.preferredWidth: 210
                                            Layout.alignment: Qt.AlignVCenter
                                            model: {
                                                var list = [];
                                                for (var i = 0; i < kityClient.availableModels.length; ++i) {
                                                    var m = kityClient.availableModels[i];
                                                    // Filter out vision-only auxiliary models from main chat LLM dropdown
                                                    if (m.indexOf("moondream") === -1) {
                                                        list.push(m);
                                                    }
                                                }
                                                if (list.length === 0) {
                                                    list.push(kityClient.ollamaModel ? kityClient.ollamaModel : "llama3.2:3b");
                                                }
                                                return list;
                                            }
                                            currentIndex: {
                                                var idx = model.indexOf(kityClient.ollamaModel);
                                                return idx >= 0 ? idx : 0;
                                            }
                                            onActivated: index => {
                                                if (index >= 0 && index < model.length) {
                                                    kityClient.setOllamaModel(model[index]);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // =====================================================
                        // Group 3: Conocimiento Local y Privacidad (RAG)
                        // =====================================================
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.Label {
                                text: i18n("📚 Conocimiento Local y Privacidad (RAG)")
                                font.bold: true
                                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize
                                color: Kirigami.Theme.highlightColor
                            }

                            Kirigami.AbstractCard {
                                Layout.fillWidth: true

                                contentItem: ColumnLayout {
                                    spacing: Kirigami.Units.mediumSpacing

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumHeight: 38
                                        spacing: Kirigami.Units.mediumSpacing
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            Layout.preferredWidth: 0
                                            spacing: 2
                                            QQC2.Label {
                                                text: i18n("Base documental privada")
                                                font.bold: true
                                                Layout.fillWidth: true
                                            }
                                            QQC2.Label {
                                                text: i18n("Kity consulta notas y documentos en tu carpeta local (~/.local/share/kity/knowledge/)")
                                                font: Kirigami.Theme.smallFont
                                                opacity: 0.75
                                                wrapMode: Text.WordWrap
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 0
                                            }
                                        }
                                        QQC2.Button {
                                            text: i18n("Abrir carpeta")
                                            icon.name: "document-open-folder"
                                            Layout.alignment: Qt.AlignVCenter
                                            onClicked: kityClient.openKnowledgeFolder()
                                        }
                                    }
                                }
                            }
                        }

                        // =====================================================
                        // Group 4: Ventana y Comportamiento
                        // =====================================================
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.Label {
                                text: i18n("🪟 Ventana y Comportamiento")
                                font.bold: true
                                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize
                                color: Kirigami.Theme.highlightColor
                            }

                            Kirigami.AbstractCard {
                                Layout.fillWidth: true

                                contentItem: ColumnLayout {
                                    spacing: Kirigami.Units.mediumSpacing

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumHeight: 38
                                        spacing: Kirigami.Units.mediumSpacing
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            Layout.preferredWidth: 0
                                            spacing: 2
                                            QQC2.Label {
                                                text: i18n("Fijar en primer plano")
                                                font.bold: true
                                                Layout.fillWidth: true
                                            }
                                            QQC2.Label {
                                                text: i18n("Mantener la ventana de Kity siempre visible sobre las demás aplicaciones")
                                                font: Kirigami.Theme.smallFont
                                                opacity: 0.75
                                                wrapMode: Text.WordWrap
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 0
                                            }
                                        }
                                        QQC2.Switch {
                                            checked: root.isPinned
                                            onToggled: root.isPinned = checked
                                        }
                                    }

                                    Kirigami.Separator { Layout.fillWidth: true }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumHeight: 38
                                        spacing: Kirigami.Units.mediumSpacing
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            Layout.preferredWidth: 0
                                            spacing: 2
                                            QQC2.Label {
                                                text: i18n("Atajos de teclado rápidos")
                                                font.bold: true
                                                Layout.fillWidth: true
                                            }
                                            QQC2.Label {
                                                text: i18n("Ctrl+M o Alt+M: Activar/detener voz · Esc: Detener generación o cerrar")
                                                font: Kirigami.Theme.smallFont
                                                opacity: 0.75
                                                wrapMode: Text.WordWrap
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 0
                                            }
                                        }
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
