/*
 * SPDX-FileCopyrightText: 2026 Kity contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import CachyOS.UI

RowLayout {
    id: root

    property bool showWritingTools: false
    property bool showVisualIntelligence: false
    property bool showMathNotes: false

    signal toggleWritingTools()
    signal toggleVisualIntelligence()
    signal toggleMathNotes()

    Layout.fillWidth: true
    spacing: CachyTheme.spacingSmall

    Rectangle {
        implicitWidth: 8
        implicitHeight: 8
        radius: 4
        color: {
            if (!Plasmoid.ollamaReachable) {
                return CachyTheme.errorColor;
            }
            if (Plasmoid.state === "listening") {
                return "#00e5ff";
            }
            if (Plasmoid.wakeWordEnabled) {
                return CachyTheme.successColor;
            }
            return CachyPalette.textMuted;
        }
    }

    Text {
        Layout.fillWidth: true
        font.pixelSize: 11
        color: CachyPalette.text
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
    CachyToolbarButton {
        id: wakeWordButton
        visible: Plasmoid.whisperAvailable
        iconName: Plasmoid.wakeWordEnabled ? "mic-on" : "mic-off"
        checkable: true
        checked: Plasmoid.wakeWordEnabled
        onToggled: (next) => {
            Plasmoid.configuration.wakeWordEnabled = next;
            Plasmoid.wakeWordEnabled = next;
        }
        tooltipText: Plasmoid.wakeWordEnabled
            ? i18n("Escucha activa «¡Hola Kity!» activada (clic para desactivar)")
            : i18n("Activar escucha manos libres «¡Hola Kity!»")
    }

    // Speech Output Mode Selector Button
    CachyToolbarButton {
        id: speechModeButton
        iconName: {
            const mode = Plasmoid.configuration.speechOutputMode;
            if (mode === "always") {
                return "audio-volume-high-symbolic";
            } else if (mode === "voice_only") {
                return "audio-input-microphone-symbolic";
            }
            return "audio-volume-muted-symbolic";
        }
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
        tooltipText: {
            const mode = Plasmoid.configuration.speechOutputMode;
            if (mode === "always") {
                return i18n("Respuestas por voz: Hablar siempre en voz alta");
            } else if (mode === "voice_only") {
                return i18n("Respuestas por voz: Hablar cuando preguntes con la voz");
            }
            return i18n("Respuestas por voz: Solo manual");
        }
    }

    // Apple Intelligence Writing Tools Toggle Button
    CachyToolbarButton {
        id: writingToolsButton
        iconName: "document-edit-symbolic"
        checkable: true
        checked: root.showWritingTools
        onToggled: root.toggleWritingTools()
        tooltipText: i18n("Herramientas de Escritura")
    }

    // Visual Intelligence & Live Text Toggle Button
    CachyToolbarButton {
        id: visualIntelButton
        iconName: "camera-photo-symbolic"
        checkable: true
        checked: root.showVisualIntelligence
        onToggled: root.toggleVisualIntelligence()
        tooltipText: i18n("Inteligencia Visual y Live Text")
    }

    // Math Notes Toggle Button
    CachyToolbarButton {
        id: mathNotesButton
        iconName: "view-time-schedule-calculus-symbolic"
        checkable: true
        checked: root.showMathNotes
        onToggled: root.toggleMathNotes()
        tooltipText: i18n("Notas matemáticas")
    }

    // Kity Mobile Remote Button
    CachyToolbarButton {
        id: mobileRemoteButton
        iconName: "kdeconnect-symbolic"
        onClicked: {
            var shared = Plasmoid.shareMobilePairingWithKdeConnect();
            if (shared) {
                Plasmoid.statusMessage = i18n("Enlace de Kity Remote enviado a tu teléfono por KDE Connect");
            } else {
                Qt.openUrlExternally(Plasmoid.mobilePairingUrl);
            }
        }
        tooltipText: i18n("Conectar teléfono como micrófono (Kity Mobile)")
    }

    // Clear Conversation Button
    CachyToolbarButton {
        iconName: "edit-clear-history-symbolic"
        visible: Plasmoid.messages.length > 0
        onClicked: Plasmoid.clearHistory()
        tooltipText: i18n("Limpiar conversación")
    }
}
