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
import org.kde.plasma.components as PlasmaComponents3

Window {
    id: root

    title: i18n("Kity")
    width: 540
    height: 760
    minimumWidth: 440
    minimumHeight: 580
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint

    property bool isPinned: false
    property bool showWritingTools: false
    property bool showVisualIntelligence: false
    property bool showMathNotes: false
    property bool showPreferences: false
    property var mathResults: []
    property string visualCaptureScope: "region"

    onIsPinnedChanged: {
        if (isPinned) {
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
        sequence: "Escape"
        onActivated: {
            if (root.showPreferences) {
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
    // Liquid Glass Container with Apple Intelligence Halo
    // =========================================================================
    Rectangle {
        id: glassContainer
        anchors.fill: parent
        radius: 22

        readonly property bool isVoiceActive: kityClient.state === "listening" || kityClient.state === "thinking" || kityClient.state === "speaking"
        readonly property bool isDark: Kirigami.Theme.colorSet === Kirigami.Theme.Window && Kirigami.Theme.backgroundColor.hslLightness < 0.5

        // Translucent Liquid Glass Background
        color: isDark ? Qt.rgba(0.09, 0.11, 0.15, 0.82) : Qt.rgba(0.96, 0.97, 0.99, 0.86)

        // Apple Intelligence Iridescent Halo / Specular Border
        border.width: isVoiceActive ? 2.5 : 1
        border.color: {
            if (kityClient.state === "listening") {
                return "#00e5ff"; // Iridescent Cyan/Turquoise
            }
            if (kityClient.state === "thinking" || kityClient.state === "transcribing") {
                return "#b388ff"; // Iridescent Violet
            }
            if (kityClient.state === "speaking") {
                return "#00e676"; // Iridescent Emerald
            }
            return isDark ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(0, 0, 0, 0.12);
        }

        Behavior on border.color {
            ColorAnimation { duration: 250 }
        }
        Behavior on border.width {
            NumberAnimation { duration: 200 }
        }

        // Inner Ambient Glow when voice is active
        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: 20
            color: glassContainer.isVoiceActive
                   ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.08)
                   : "transparent"
            border.width: 1
            border.color: glassContainer.isVoiceActive
                          ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.25)
                          : "transparent"
            Behavior on color { ColorAnimation { duration: 250 } }
            Behavior on border.color { ColorAnimation { duration: 250 } }
        }

        // Specular Top Shine
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 2
            height: 1
            color: Qt.rgba(1, 1, 1, 0.35)
            radius: 22
        }

        // =====================================================================
        // Window Content Layout
        // =====================================================================
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Kirigami.Units.mediumSpacing
            spacing: Kirigami.Units.smallSpacing

            // -----------------------------------------------------------------
            // Top Window Bar: Traffic Lights, Siri Status Pill, Preferences & Pin
            // -----------------------------------------------------------------
            Item {
                Layout.fillWidth: true
                implicitHeight: 38

                // Window Drag Handler
                DragHandler {
                    target: null
                    onActiveChanged: {
                        if (active) root.startSystemMove();
                    }
                }

                // Left: Traffic Light Buttons
                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: Kirigami.Units.smallSpacing
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    // Close (Red)
                    Rectangle {
                        width: 12
                        height: 12
                        radius: 6
                        color: closeHover.hovered ? "#ff5f56" : "#ff5f57"
                        border.width: 0.5
                        border.color: "#e0443e"
                        HoverHandler { id: closeHover }
                        TapHandler { onTapped: root.close() }
                        Text {
                            anchors.centerIn: parent
                            text: "×"
                            font.pixelSize: 10
                            font.bold: true
                            color: "#4d0000"
                            visible: closeHover.hovered
                        }
                    }

                    // Minimize (Yellow)
                    Rectangle {
                        width: 12
                        height: 12
                        radius: 6
                        color: minHover.hovered ? "#ffbd2e" : "#febc2e"
                        border.width: 0.5
                        border.color: "#dea123"
                        HoverHandler { id: minHover }
                        TapHandler { onTapped: root.showMinimized() }
                        Text {
                            anchors.centerIn: parent
                            text: "–"
                            font.pixelSize: 10
                            font.bold: true
                            color: "#593b00"
                            visible: minHover.hovered
                        }
                    }

                    // Maximize (Green)
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

                // Center: Glowing Iridescent Siri Pill
                Rectangle {
                    anchors.centerIn: parent
                    height: 26
                    width: siriPillRow.implicitWidth + 20
                    radius: 13
                    color: Qt.rgba(1, 1, 1, 0.08)
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.14)

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

                        PlasmaComponents3.Label {
                            text: i18n("Kity")
                            font.bold: true
                            font.pixelSize: Kirigami.Theme.defaultFont.pixelSize
                        }
                    }
                }

                // Right: Preferences & Pin
                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: Kirigami.Units.smallSpacing
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    // Pin Window On Top
                    PlasmaComponents3.ToolButton {
                        icon.name: root.isPinned ? "window-pin" : "window-unpin"
                        checkable: true
                        checked: root.isPinned
                        QQC2.ToolTip.text: root.isPinned ? i18n("Desanclar de primer plano") : i18n("Mantener siempre encima")
                        QQC2.ToolTip.visible: hovered
                        onClicked: root.isPinned = !root.isPinned
                    }

                    // Preferences Button
                    PlasmaComponents3.ToolButton {
                        icon.name: "configure"
                        QQC2.ToolTip.text: i18n("Preferencias de Kity")
                        QQC2.ToolTip.visible: hovered
                        onClicked: root.showPreferences = !root.showPreferences
                    }
                }
            }

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

                PlasmaComponents3.Label {
                    Layout.fillWidth: true
                    font: Kirigami.Theme.smallFont
                    opacity: 0.85
                    elide: Text.ElideRight
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
                PlasmaComponents3.ToolButton {
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
                PlasmaComponents3.ToolButton {
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
                PlasmaComponents3.ToolButton {
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
                PlasmaComponents3.ToolButton {
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
                PlasmaComponents3.ToolButton {
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

                // Clear Conversation Button
                PlasmaComponents3.ToolButton {
                    icon.name: "edit-clear-history-symbolic"
                    visible: kityClient.messages.length > 0
                    onClicked: kityClient.clearHistory()
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: i18n("Limpiar conversación")
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            // -----------------------------------------------------------------
            // Writing Tools Panel Drawer (Phase 2 - 8 Tools)
            // -----------------------------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                visible: root.showWritingTools
                implicitHeight: wtCol.implicitHeight + Kirigami.Units.smallSpacing * 2
                radius: Kirigami.Units.smallSpacing
                color: Kirigami.Theme.alternateBackgroundColor
                border.width: 1
                border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.14)

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

                        PlasmaComponents3.Label {
                            Layout.fillWidth: true
                            font.bold: true
                            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                            text: promptField.text.trim().length > 0
                                ? i18n("Herramientas de Escritura (usando caja de texto)")
                                : i18n("Herramientas de Escritura (usando selección / portapapeles Wayland)")
                            elide: Text.ElideRight
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 4
                        columnSpacing: Kirigami.Units.smallSpacing
                        rowSpacing: Kirigami.Units.smallSpacing
                        uniformCellWidths: true

                        PlasmaComponents3.Button {
                            Layout.fillWidth: true
                            text: i18n("✍️ Corregir")
                            onClicked: {
                                kityClient.applyWritingTool("proofread", promptField.text);
                                root.showWritingTools = false;
                            }
                        }
                        PlasmaComponents3.Button {
                            Layout.fillWidth: true
                            text: i18n("📝 Reescribir")
                            onClicked: {
                                kityClient.applyWritingTool("rewrite", promptField.text);
                                root.showWritingTools = false;
                            }
                        }
                        PlasmaComponents3.Button {
                            Layout.fillWidth: true
                            text: i18n("😊 Amigable")
                            onClicked: {
                                kityClient.applyWritingTool("friendly", promptField.text);
                                root.showWritingTools = false;
                            }
                        }
                        PlasmaComponents3.Button {
                            Layout.fillWidth: true
                            text: i18n("👔 Profesional")
                            onClicked: {
                                kityClient.applyWritingTool("professional", promptField.text);
                                root.showWritingTools = false;
                            }
                        }
                        PlasmaComponents3.Button {
                            Layout.fillWidth: true
                            text: i18n("✂️ Conciso")
                            onClicked: {
                                kityClient.applyWritingTool("concise", promptField.text);
                                root.showWritingTools = false;
                            }
                        }
                        PlasmaComponents3.Button {
                            Layout.fillWidth: true
                            text: i18n("📊 Resumen")
                            onClicked: {
                                kityClient.applyWritingTool("summary", promptField.text);
                                root.showWritingTools = false;
                            }
                        }
                        PlasmaComponents3.Button {
                            Layout.fillWidth: true
                            text: i18n("📌 Claves")
                            onClicked: {
                                kityClient.applyWritingTool("keypoints", promptField.text);
                                root.showWritingTools = false;
                            }
                        }
                        PlasmaComponents3.Button {
                            Layout.fillWidth: true
                            text: i18n("📋 Tabla")
                            onClicked: {
                                kityClient.applyWritingTool("table", promptField.text);
                                root.showWritingTools = false;
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
                implicitHeight: viCol.implicitHeight + Kirigami.Units.smallSpacing * 2
                radius: Kirigami.Units.smallSpacing
                color: Kirigami.Theme.alternateBackgroundColor
                border.width: 1
                border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.14)

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

                        PlasmaComponents3.Label {
                            Layout.fillWidth: true
                            font.bold: true
                            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                            text: i18n("Inteligencia Visual (Spectacle Wayland + Tesseract OCR)")
                            elide: Text.ElideRight
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        PlasmaComponents3.Label {
                            text: i18n("Capturar:")
                            font: Kirigami.Theme.smallFont
                        }

                        PlasmaComponents3.ComboBox {
                            id: viScopeCombo
                            implicitWidth: 140
                            model: [
                                { label: i18n("Región"), scope: "region" },
                                { label: i18n("Ventana activa"), scope: "window" },
                                { label: i18n("Pantalla completa"), scope: "fullscreen" }
                            ]
                            textRole: "label"
                            onActivated: index => root.visualCaptureScope = model[index].scope
                        }

                        PlasmaComponents3.Button {
                            text: i18n("📄 Extraer texto (OCR)")
                            icon.name: "document-edit-symbolic"
                            onClicked: {
                                kityClient.runVisualIntelligence(root.visualCaptureScope, "ocr");
                                root.showVisualIntelligence = false;
                            }
                        }

                        PlasmaComponents3.Button {
                            text: i18n("🔍 Describir")
                            icon.name: "image-x-generic-symbolic"
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
                implicitHeight: mnCol.implicitHeight + Kirigami.Units.smallSpacing * 2
                radius: Kirigami.Units.smallSpacing
                color: Kirigami.Theme.alternateBackgroundColor
                border.width: 1
                border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.14)

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

                        PlasmaComponents3.Label {
                            Layout.fillWidth: true
                            font.bold: true
                            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                            text: i18n("Notas matemáticas (escribe operaciones y pulsa =)")
                            elide: Text.ElideRight
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        PlasmaComponents3.TextField {
                            id: mathInput
                            Layout.fillWidth: true
                            placeholderText: i18n("Ejemplo: precio = 250 €, iva = precio * 0.21, total =")
                            onAccepted: {
                                if (text.trim().length > 0) {
                                    root.mathResults = kityClient.evaluateMathNotes(text);
                                }
                            }
                        }

                        PlasmaComponents3.Button {
                            text: i18n("Calcular")
                            icon.name: "checkmark"
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
                            delegate: PlasmaComponents3.Label {
                                required property string modelData
                                Layout.fillWidth: true
                                font.family: "monospace"
                                text: modelData
                            }
                        }
                    }
                }
            }

            // -----------------------------------------------------------------
            // Central Chat & Welcome Area (From Plasmoid)
            // -----------------------------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Kirigami.Units.smallSpacing
                color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.alternateBackgroundColor, 0.4)
                border.width: 1
                border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.14)

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

                    PlasmaComponents3.Label {
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
                                { label: "⚡ Estado del sistema", prompt: "Estado del sistema", isWritingTool: false },
                                { label: "🎵 ¿Qué canción suena?", prompt: "¿Qué canción está sonando?", isWritingTool: false },
                                { label: "🌙 Modo oscuro", prompt: "Activa el modo oscuro", isWritingTool: false }
                            ]

                            PlasmaComponents3.Button {
                                required property var modelData
                                Layout.fillWidth: true
                                text: modelData.label
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

                                                PlasmaComponents3.Label {
                                                    Layout.fillWidth: true
                                                    font.family: "monospace"
                                                    text: modelData
                                                    elide: Text.ElideRight
                                                }

                                                PlasmaComponents3.Button {
                                                    icon.name: "system-run"
                                                    text: i18n("Ejecutar en Konsole")
                                                    onClicked: kityClient.executeCommandInKonsole(modelData)
                                                }
                                            }
                                        }
                                    }

                                    RowLayout {
                                        visible: !msgDelegate.isUser
                                        Layout.fillWidth: true
                                        spacing: Kirigami.Units.smallSpacing

                                        PlasmaComponents3.ToolButton {
                                            icon.name: "edit-copy"
                                            QQC2.ToolTip.visible: hovered
                                            QQC2.ToolTip.text: i18n("Copiar al portapapeles")
                                            onClicked: kityClient.copyToClipboard(msgDelegate.modelData.content)
                                        }

                                        PlasmaComponents3.ToolButton {
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

            // -----------------------------------------------------------------
            // Bottom Dock: Dynamic Animated Siri Orb & Text Input (From Plasmoid)
            // -----------------------------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: dockCol.implicitHeight + Kirigami.Units.smallSpacing * 2
                radius: Kirigami.Units.smallSpacing
                color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.alternateBackgroundColor, 0.4)
                border.width: 1
                border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.14)

                ColumnLayout {
                    id: dockCol
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    // Siri Orb Centered Interactive Voice Control (Dynamic Audio Reactivity)
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: Kirigami.Units.gridUnit * 3.8
                        clip: true

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

                    // Bottom Text Input & Send Bar
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

                        PlasmaComponents3.Button {
                            Layout.alignment: Qt.AlignBottom
                            icon.name: kityClient.state === "thinking" ? "process-stop-symbolic" : "document-send-symbolic"
                            enabled: kityClient.state === "thinking" || promptField.text.trim().length > 0
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

        // =====================================================================
        // Redesigned Preferences Modal (Kirigami Liquid Glass Sheet)
        // =====================================================================
        Rectangle {
            id: preferencesModal
            anchors.fill: parent
            radius: 22
            visible: root.showPreferences
            color: glassContainer.isDark ? Qt.rgba(0.08, 0.10, 0.14, 0.96) : Qt.rgba(0.97, 0.98, 1.0, 0.96)
            border.width: 1
            border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.20)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.mediumSpacing

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

                    PlasmaComponents3.ToolButton {
                        icon.name: "window-close"
                        onClicked: root.showPreferences = false
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.text: i18n("Cerrar preferencias")
                    }
                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                // Scrollable Settings Cards
                QQC2.ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    ColumnLayout {
                        width: parent.width
                        spacing: Kirigami.Units.mediumSpacing

                        // Card 1: Voice & Wake Word
                        Kirigami.Card {
                            Layout.fillWidth: true
                            banner.title: i18n("🎙️ Voz y Activación Manos Libres")

                            contentItem: ColumnLayout {
                                spacing: Kirigami.Units.smallSpacing

                                PlasmaComponents3.CheckBox {
                                    text: i18n("Escuchar «¡Hola Kity!» u «Oye Kity» en segundo plano")
                                    checked: kityClient.wakeWordEnabled
                                    onToggled: kityClient.wakeWordEnabled = checked
                                }

                                PlasmaComponents3.CheckBox {
                                    text: i18n("Detección de silencio VAD (detener al terminar de hablar)")
                                    checked: kityClient.vadEnabled
                                    onToggled: kityClient.vadEnabled = checked
                                }

                                PlasmaComponents3.CheckBox {
                                    text: i18n("Avisos acústicos (tono armónico al estilo Siri)")
                                    checked: kityClient.earconsEnabled
                                    onToggled: kityClient.earconsEnabled = checked
                                }

                                PlasmaComponents3.Label {
                                    text: i18n("Modelo Whisper STT: %1", kityClient.whisperAvailable ? i18n("Disponible (ggml-base.bin)") : i18n("No encontrado"))
                                    color: kityClient.whisperAvailable ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor
                                }
                            }
                        }

                        // Card 2: Spoken Responses (TTS)
                        Kirigami.Card {
                            Layout.fillWidth: true
                            banner.title: i18n("🔊 Síntesis de Voz Neuronal (Piper TTS)")

                            contentItem: ColumnLayout {
                                spacing: Kirigami.Units.smallSpacing

                                PlasmaComponents3.Label {
                                    text: i18n("Modo de respuestas habladas:")
                                    font.bold: true
                                }

                                RowLayout {
                                    spacing: Kirigami.Units.smallSpacing

                                    PlasmaComponents3.RadioButton {
                                        text: i18n("Siempre")
                                        checked: kityClient.speechOutputMode === "always"
                                        onToggled: if (checked) kityClient.setSpeechOutputMode("always")
                                    }

                                    PlasmaComponents3.RadioButton {
                                        text: i18n("Solo por voz")
                                        checked: kityClient.speechOutputMode === "voice_only"
                                        onToggled: if (checked) kityClient.setSpeechOutputMode("voice_only")
                                    }

                                    PlasmaComponents3.RadioButton {
                                        text: i18n("Manual")
                                        checked: kityClient.speechOutputMode === "manual"
                                        onToggled: if (checked) kityClient.setSpeechOutputMode("manual")
                                    }
                                }

                                PlasmaComponents3.Label {
                                    text: i18n("Voz Piper: %1", kityClient.piperAvailable ? i18n("Disponible (es_ES-sharvard-medium)") : i18n("No encontrada"))
                                    color: kityClient.piperAvailable ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor
                                }
                            }
                        }

                        // Card 3: AI Engine (Ollama)
                        Kirigami.Card {
                            Layout.fillWidth: true
                            banner.title: i18n("🧠 Inteligencia Artificial Local (Ollama)")

                            contentItem: ColumnLayout {
                                spacing: Kirigami.Units.smallSpacing

                                RowLayout {
                                    Layout.fillWidth: true

                                    Rectangle {
                                        implicitWidth: 10
                                        implicitHeight: 10
                                        radius: 5
                                        color: kityClient.ollamaReachable ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor
                                    }

                                    PlasmaComponents3.Label {
                                        Layout.fillWidth: true
                                        text: kityClient.ollamaReachable
                                            ? i18n("Servidor Ollama conectado en localhost:11434")
                                            : i18n("Servidor Ollama no detectado en localhost:11434")
                                        color: kityClient.ollamaReachable ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor
                                    }

                                    PlasmaComponents3.Button {
                                        text: i18n("Refrescar")
                                        icon.name: "view-refresh"
                                        onClicked: {
                                            kityClient.checkReadiness();
                                            kityClient.refreshModels();
                                        }
                                    }
                                }

                                PlasmaComponents3.Label {
                                    text: i18n("Modelos instalados detectados: %1", kityClient.availableModels.join(", "))
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                    font: Kirigami.Theme.smallFont
                                }
                            }
                        }

                        // Card 4: Local Knowledge Base (RAG)
                        Kirigami.Card {
                            Layout.fillWidth: true
                            banner.title: i18n("📚 Conocimiento Local (RAG)")

                            contentItem: ColumnLayout {
                                spacing: Kirigami.Units.smallSpacing

                                PlasmaComponents3.Label {
                                    text: i18n("Kity consulta automáticamente documentos y apuntes guardados en tu carpeta local para dar respuestas precisas y privadas.")
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                    font: Kirigami.Theme.smallFont
                                }

                                PlasmaComponents3.Button {
                                    icon.name: "document-open-folder"
                                    text: i18n("Abrir carpeta de notas en Dolphin (~/.local/share/kity/knowledge/)")
                                    onClicked: kityClient.openKnowledgeFolder()
                                }
                            }
                        }
                    }
                }

                // Close Button
                PlasmaComponents3.Button {
                    Layout.alignment: Qt.AlignRight
                    text: i18n("Cerrar")
                    icon.name: "dialog-ok"
                    onClicked: root.showPreferences = false
                }
            }
        }
    }

    // Window Resizer Corner Handler
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
