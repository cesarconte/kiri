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

RowLayout {
    id: root

    property bool showWritingTools: false
    property bool showVisualIntelligence: false
    property bool showMathNotes: false

    signal toggleWritingTools()
    signal toggleVisualIntelligence()
    signal toggleMathNotes()

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

    QQC2.Label {
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
                return i18n("«¡Hola Kity!» activo");
            }
            return i18n("Listo");
        }
    }

    // Quick Toggle for "¡Hola Kity!" Wake Word
    QQC2.ToolButton {
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
            ? i18n("Escucha activa «¡Hola Kity!» activada (clic para desactivar)")
            : i18n("Activar escucha manos libres «¡Hola Kity!»")
    }

    // Speech Output Mode Selector Button
    QQC2.ToolButton {
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

    // Apple Intelligence Writing Tools Toggle Button
    QQC2.ToolButton {
        id: writingToolsButton
        icon.name: "document-edit-symbolic"
        icon.color: checked ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
        checkable: true
        checked: root.showWritingTools
        onClicked: root.toggleWritingTools()
        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.text: i18n("Herramientas de Escritura (Corregir, Reescribir, Tono y Resumen sobre texto seleccionado o escrito)")
    }

    // Visual Intelligence & Live Text Toggle Button
    QQC2.ToolButton {
        id: visualIntelButton
        icon.name: "camera-photo-symbolic"
        icon.color: checked ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
        checkable: true
        checked: root.showVisualIntelligence
        onClicked: root.toggleVisualIntelligence()
        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.text: i18n("Inteligencia Visual y Live Text (Capturar región o ventana con Spectacle + OCR + IA)")
    }

    // Math Notes Toggle Button
    QQC2.ToolButton {
        id: mathNotesButton
        icon.name: "view-time-schedule-calculus-symbolic"
        icon.color: checked ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
        checkable: true
        checked: root.showMathNotes
        onClicked: root.toggleMathNotes()
        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.text: i18n("Notas matemáticas (cálculo en vivo con variables, unidades y divisas)")
    }

    // Kity Mobile Remote Button
    QQC2.ToolButton {
        id: mobileRemoteButton
        icon.name: "kdeconnect-symbolic"
        onClicked: {
            var shared = Plasmoid.shareMobilePairingWithKdeConnect();
            if (shared) {
                Plasmoid.statusMessage = i18n("Enlace de Kity Remote enviado a tu teléfono por KDE Connect");
            } else {
                Qt.openUrlExternally(Plasmoid.mobilePairingUrl);
            }
        }
        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.text: i18n("Conectar teléfono como micrófono (Kity Mobile Portal)")
    }

    // Clear Conversation Button
    QQC2.ToolButton {
        icon.name: "edit-clear-history-symbolic"
        visible: Plasmoid.messages.length > 0
        onClicked: Plasmoid.clearHistory()
        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.text: i18n("Limpiar conversación")
    }
}
