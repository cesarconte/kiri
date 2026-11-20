/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import CachyOS.UI
import org.kde.plasma.plasmoid

Item {
    id: expandedRoot

    property var widget: null
    property bool showWritingTools: false
    property bool showVisualIntelligence: false
    property bool showMathNotes: false
    property var mathResults: []
    property string visualCaptureScope: "region"

    implicitWidth: 384
    implicitHeight: 480

    Layout.minimumWidth: 320
    Layout.minimumHeight: 384
    Layout.preferredWidth: 384
    Layout.preferredHeight: 480
    Layout.maximumWidth: 576
    Layout.maximumHeight: 640

    function focusInput(): void {
        bottomDock.focusInput();
    }

    function formatTimestamp(raw: string): string {
        return chatView.formatTimestamp(raw);
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

    // Voice interaction glow: color changes smoothly when listening or speaking
    Rectangle {
        id: voiceInteractionGlow
        anchors.fill: parent
        anchors.margins: 2
        radius: CachyTheme.radiusSmall
        readonly property bool isVoiceActive: Plasmoid.state === "listening" || Plasmoid.state === "speaking"
        color: isVoiceActive
            ? Qt.rgba(CachyTheme.accentColor.r, CachyTheme.accentColor.g, CachyTheme.accentColor.b, 0.12)
            : "transparent"
        border.width: isVoiceActive ? 1 : 0
        border.color: CachyTheme.accentColor

        Behavior on color { ColorAnimation { duration: 250 } }
        Behavior on border.width { NumberAnimation { duration: 200 } }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: CachyTheme.spacingMedium
        spacing: CachyTheme.spacingSmall

        // Top Status & Quick Controls Bar
        TopControlsBar {
            id: topControls
            showWritingTools: expandedRoot.showWritingTools
            showVisualIntelligence: expandedRoot.showVisualIntelligence
            showMathNotes: expandedRoot.showMathNotes

            onToggleWritingTools: {
                expandedRoot.showWritingTools = !expandedRoot.showWritingTools;
                if (expandedRoot.showWritingTools) {
                    expandedRoot.showVisualIntelligence = false;
                    expandedRoot.showMathNotes = false;
                }
            }

            onToggleVisualIntelligence: {
                expandedRoot.showVisualIntelligence = !expandedRoot.showVisualIntelligence;
                if (expandedRoot.showVisualIntelligence) {
                    expandedRoot.showWritingTools = false;
                    expandedRoot.showMathNotes = false;
                }
            }

            onToggleMathNotes: {
                expandedRoot.showMathNotes = !expandedRoot.showMathNotes;
                if (expandedRoot.showMathNotes) {
                    expandedRoot.showWritingTools = false;
                    expandedRoot.showVisualIntelligence = false;
                    Qt.callLater(() => mathNotesPanel.focusInput());
                }
            }
        }

        CachyDivider {
            Layout.fillWidth: true
        }

        // Apple Intelligence Writing Tools Panel
        WritingToolsPanel {
            id: writingToolsPanel
            visible: expandedRoot.showWritingTools
            promptText: bottomDock.promptText
            onToolTriggered: (action, customText) => {
                Plasmoid.applyWritingTool(action, customText);
                if (customText.length > 0) {
                    bottomDock.promptText = "";
                }
            }
        }

        // Math Notes Panel
        MathNotesPanel {
            id: mathNotesPanel
            visible: expandedRoot.showMathNotes
            mathResults: expandedRoot.mathResults
            onMathResultsChanged: expandedRoot.mathResults = mathResults
        }

        // Visual Intelligence & Live Text Panel
        VisualIntelligencePanel {
            id: visualIntelPanel
            visible: expandedRoot.showVisualIntelligence
            visualCaptureScope: expandedRoot.visualCaptureScope
            promptText: bottomDock.promptText
            onVisualTriggered: (scope, action, customQ) => {
                expandedRoot.visualCaptureScope = scope;
                Plasmoid.runVisualIntelligence(scope, action, customQ);
                if (customQ.length > 0 && action === "explain") {
                    bottomDock.promptText = "";
                }
            }
        }

        // Status / Readiness Inline Message
        CachyInlineMessage {
            Layout.fillWidth: true
            visible: !Plasmoid.ollamaReachable
            messageType: "warning"
            text: i18n("No se puede conectar con Ollama en %1. Ejecuta scripts/setup-models.sh o inicia ollama.service.", Plasmoid.configuration.ollamaUrl)
        }

        // Main Chat & Quick Actions View
        ChatView {
            id: chatView
            onQuickActionTriggered: (prompt, isWritingTool) => {
                if (isWritingTool) {
                    expandedRoot.showWritingTools = true;
                } else {
                    Plasmoid.sendMessage(prompt, false);
                }
            }
        }

        // Dynamic status bar when recording, transcribing, thinking, or speaking
        QQC2.Label {
            Layout.fillWidth: true
            visible: Plasmoid.statusMessage.length > 0 && Plasmoid.state !== "idle"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: Plasmoid.statusMessage
            font.pixelSize: 11
            color: Plasmoid.state === "error" ? CachyTheme.errorColor : CachyTheme.accentColor
            wrapMode: Text.WordWrap
        }

        // Bottom Dock (Siri Orb + Prompt Input Bar)
        BottomDock {
            id: bottomDock
            onMessageSubmitted: text => Plasmoid.sendMessage(text, false)
        }
    }
}
