/*
    SPDX-FileCopyrightText: 2026 Kiri contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtCore
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs as QtDialogs

import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami

KCMUtils.SimpleKCM {
    id: configRoot

    // =========================================================================
    // KConfigXT Properties Bindings (cfg_<entryName>)
    // =========================================================================

    // Ollama AI Engine
    property alias cfg_ollamaUrl: ollamaUrlField.text
    property string cfg_ollamaModel: "qwen2.5:7b"
    property alias cfg_ollamaVisionModel: ollamaVisionModelField.text
    property double cfg_ollamaTemperature: 0.2
    property alias cfg_ollamaNumCtx: ollamaCtxSpinBox.value
    property alias cfg_ollamaNumThread: ollamaThreadSpinBox.value
    property alias cfg_ollamaNumPredict: ollamaPredictSpinBox.value
    property alias cfg_ollamaKeepAlive: ollamaKeepAliveField.text
    property alias cfg_systemPrompt: systemPromptArea.text

    // Local RAG Knowledge Base
    property alias cfg_knowledgeBaseEnabled: kbEnabledCheck.checked
    property alias cfg_knowledgeBaseMaxSnippets: kbSnippetsSpinBox.value

    // Appearance & Behavior
    property string cfg_panelIconStyle: "symbolic"
    property alias cfg_pin: pinWindowCheck.checked

    // Speech-to-Text Input (PipeWire + whisper.cpp)
    property alias cfg_autoListenOnActivate: autoListenCheck.checked
    property alias cfg_vadEnabled: vadEnabledCheck.checked
    property alias cfg_vadSilenceTimeoutMs: vadSilenceSpinBox.value
    property alias cfg_wakeWordEnabled: wakeWordCheck.checked
    property alias cfg_whisperLanguage: whisperLangField.text
    property alias cfg_whisperModelPath: whisperModelField.text

    // Text-to-Speech Output (PipeWire + Piper TTS)
    property string cfg_speechOutputMode: "always"
    property alias cfg_piperVoicePath: piperVoiceField.text

    // Communications
    property string cfg_defaultEmailClient: "proton"
    property string cfg_defaultCalendarClient: "proton"

    // =========================================================================
    // Local State & Dynamic Ollama Model Discovery
    // =========================================================================

    property var detectedModels: ["qwen2.5:7b"]

    function fetchOllamaModels() {
        const baseUrl = (ollamaUrlField.text.trim() || "http://127.0.0.1:11434");
        const url = baseUrl + "/api/tags";
        const xhr = new XMLHttpRequest();
        xhr.open("GET", url);
        xhr.timeout = 3000;
        xhr.onreadystatechange = function() {
            if (xhr.readyState === XMLHttpRequest.DONE && xhr.status === 200) {
                try {
                    const data = JSON.parse(xhr.responseText);
                    const list = [];
                    if (data.models && Array.isArray(data.models)) {
                        for (let i = 0; i < data.models.length; ++i) {
                            if (data.models[i].name) {
                                list.push(data.models[i].name);
                            }
                        }
                    }
                    if (list.length > 0) {
                        if (configRoot.cfg_ollamaModel && list.indexOf(configRoot.cfg_ollamaModel) === -1) {
                            list.unshift(configRoot.cfg_ollamaModel);
                        }
                        configRoot.detectedModels = list;
                    }
                } catch (e) {
                }
            }
        };
        xhr.send();
    }

    Component.onCompleted: {
        fetchOllamaModels();
    }

    // =========================================================================
    // Option Models
    // =========================================================================

    readonly property var panelIconStyleModel: [
        {
            "value": "symbolic",
            "text": i18n("Symbolic monochrome (Breeze, recommended)")
        },
        {
            "value": "color",
            "text": i18n("Full color (Breeze gradient)")
        }
    ]

    readonly property var emailClientModel: [
        {
            "value": "proton",
            "text": i18n("Proton Mail (Desktop App)")
        },
        {
            "value": "gmail",
            "text": i18n("Google Gmail (Web Browser)")
        }
    ]

    readonly property var calendarClientModel: [
        {
            "value": "proton",
            "text": i18n("Proton Calendar (Desktop App)")
        },
        {
            "value": "google",
            "text": i18n("Google Calendar (Web Browser)")
        }
    ]

    readonly property var speechModeModel: [
        {
            "value": "voice_only",
            "text": i18nc("@item:inlistbox Speech output mode", "When asked by voice (Recommended)")
        },
        {
            "value": "always",
            "text": i18nc("@item:inlistbox Speech output mode", "Always speak every response aloud")
        },
        {
            "value": "manual",
            "text": i18nc("@item:inlistbox Speech output mode", "Manual only (when clicking Read Aloud)")
        }
    ]

    // =========================================================================
    // File Selection Dialogs
    // =========================================================================

    QtDialogs.FileDialog {
        id: whisperFileDialog
        title: i18n("Select Whisper GGML Model (.bin)")
        nameFilters: [i18n("Whisper GGML models (*.bin)"), i18n("All files (*)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.GenericDataLocation) + "/kiri/models"
        onAccepted: {
            whisperModelField.text = selectedFile.toString().replace(/^file:\/\//, "");
        }
    }

    QtDialogs.FileDialog {
        id: piperFileDialog
        title: i18n("Select Piper Voice Model (.onnx)")
        nameFilters: [i18n("Piper ONNX models (*.onnx)"), i18n("All files (*)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.GenericDataLocation) + "/kiri/voices"
        onAccepted: {
            piperVoiceField.text = selectedFile.toString().replace(/^file:\/\//, "");
        }
    }

    // =========================================================================
    // Form Layout
    // =========================================================================

    Kirigami.FormLayout {
        // ---------------------------------------------------------------------
        // 1. Ollama AI Engine Section
        // ---------------------------------------------------------------------
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Ollama AI Engine")
        }

        QQC2.TextField {
            id: ollamaUrlField
            Kirigami.FormData.label: i18n("Ollama Server URL:")
            placeholderText: "http://127.0.0.1:11434"
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Default Model:")
            Layout.fillWidth: true
            Layout.minimumWidth: 0

            QQC2.ComboBox {
                id: ollamaModelCombo
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 0
                editable: true
                model: configRoot.detectedModels
                editText: configRoot.cfg_ollamaModel
                currentIndex: {
                    const idx = model.indexOf(configRoot.cfg_ollamaModel);
                    return idx >= 0 ? idx : 0;
                }
                onActivated: index => {
                    configRoot.cfg_ollamaModel = model[index];
                }
                onAccepted: {
                    if (editText.trim().length > 0) {
                        configRoot.cfg_ollamaModel = editText.trim();
                    }
                }
                QQC2.ToolTip.text: i18n("Select or type the model name to use for chat inference")
                QQC2.ToolTip.visible: hovered
            }

            QQC2.Button {
                icon.name: "view-refresh-symbolic"
                text: i18n("Detect")
                QQC2.ToolTip.text: i18n("Refresh installed models from Ollama server")
                QQC2.ToolTip.visible: hovered
                onClicked: configRoot.fetchOllamaModels()
            }
        }

        QQC2.TextField {
            id: ollamaVisionModelField
            Kirigami.FormData.label: i18n("Vision Model:")
            placeholderText: "moondream"
            QQC2.ToolTip.text: i18n("Model used for analyzing screenshot captures (e.g. moondream, llava, minicpm-v)")
            QQC2.ToolTip.visible: hovered
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Temperature (%1):", configRoot.cfg_ollamaTemperature.toFixed(2))
            Layout.fillWidth: true
            Layout.minimumWidth: 0

            QQC2.Slider {
                id: tempSlider
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 0
                from: 0
                to: 100
                stepSize: 5
                value: Math.round(configRoot.cfg_ollamaTemperature * 100)
                onMoved: configRoot.cfg_ollamaTemperature = value / 100.0
            }
            QQC2.Label {
                text: configRoot.cfg_ollamaTemperature <= 0.3 ? i18n("Factual (less hallucinations)") : i18n("Creative")
                color: Kirigami.Theme.neutralTextColor
            }
        }

        QQC2.SpinBox {
            id: ollamaCtxSpinBox
            Kirigami.FormData.label: i18n("Context Window (tokens):")
            from: 1024
            to: 65536
            stepSize: 1024
            editable: true
            QQC2.ToolTip.text: i18n("Maximum context memory window size passed to the model")
            QQC2.ToolTip.visible: hovered
        }

        QQC2.SpinBox {
            id: ollamaThreadSpinBox
            Kirigami.FormData.label: i18n("CPU Inference Threads:")
            from: 1
            to: 64
            stepSize: 1
            editable: true
            QQC2.ToolTip.text: i18n("Recommended: number of physical performance CPU cores")
            QQC2.ToolTip.visible: hovered
        }

        QQC2.SpinBox {
            id: ollamaPredictSpinBox
            Kirigami.FormData.label: i18n("Max Generated Tokens:")
            from: 64
            to: 4096
            stepSize: 64
            editable: true
            QQC2.ToolTip.text: i18n("Maximum number of tokens generated per response turn")
            QQC2.ToolTip.visible: hovered
        }

        QQC2.TextField {
            id: ollamaKeepAliveField
            Kirigami.FormData.label: i18n("RAM Keep-Alive:")
            placeholderText: "24h"
            QQC2.ToolTip.text: i18n("How long the model stays resident in GPU/VRAM between turns (e.g. 5m, 1h, 24h)")
            QQC2.ToolTip.visible: hovered
        }

        QQC2.TextArea {
            id: systemPromptArea
            Kirigami.FormData.label: i18n("System Prompt:")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 0
            implicitHeight: Kirigami.Units.gridUnit * 5
            wrapMode: TextEdit.Wrap
            QQC2.ToolTip.text: i18n("Base personality and instructions passed to the assistant")
            QQC2.ToolTip.visible: hovered
        }

        // ---------------------------------------------------------------------
        // 2. Local Knowledge Base (RAG)
        // ---------------------------------------------------------------------
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Local Knowledge Base (RAG)")
        }

        QQC2.CheckBox {
            id: kbEnabledCheck
            Kirigami.FormData.label: i18n("Local RAG:")
            text: i18n("Search local documents to augment answers")
            checked: true
            QQC2.ToolTip.text: i18n("Place personal .txt or .md notes in ~/.local/share/kiri/knowledge/ to allow Kiri to securely search them offline")
            QQC2.ToolTip.visible: hovered
        }

        QQC2.SpinBox {
            id: kbSnippetsSpinBox
            Kirigami.FormData.label: i18n("Max Snippets:")
            from: 1
            to: 10
            stepSize: 1
            value: 3
            editable: true
            enabled: kbEnabledCheck.checked
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Documents Folder:")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            enabled: kbEnabledCheck.checked

            QQC2.Label {
                text: "~/.local/share/kiri/knowledge/"
                font.family: "monospace"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideMiddle
            }

            QQC2.Button {
                icon.name: "document-open-folder"
                text: i18n("Open in Dolphin")
                QQC2.ToolTip.text: i18n("Open knowledge directory in Dolphin to add Markdown or text notes")
                QQC2.ToolTip.visible: hovered
                onClicked: {
                    const path = StandardPaths.writableLocation(StandardPaths.GenericDataLocation) + "/kiri/knowledge";
                    Qt.openUrlExternally("file://" + path);
                }
            }
        }

        // ---------------------------------------------------------------------
        // 3. Appearance & Behavior Section
        // ---------------------------------------------------------------------
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Appearance & Behavior")
        }

        QQC2.ComboBox {
            id: panelIconStyleCombo
            Kirigami.FormData.label: i18n("Panel Icon Style:")
            model: configRoot.panelIconStyleModel
            textRole: "text"
            valueRole: "value"
            currentIndex: {
                for (let i = 0; i < configRoot.panelIconStyleModel.length; ++i) {
                    if (configRoot.panelIconStyleModel[i].value === configRoot.cfg_panelIconStyle) {
                        return i;
                    }
                }
                return 0;
            }
            onActivated: index => {
                configRoot.cfg_panelIconStyle = configRoot.panelIconStyleModel[index].value;
            }
        }

        QQC2.CheckBox {
            id: pinWindowCheck
            Kirigami.FormData.label: i18n("Window Behavior:")
            text: i18n("Keep popup open when focus moves to another window (Pin)")
        }

        // ---------------------------------------------------------------------
        // 4. Voice Input (PipeWire & whisper.cpp)
        // ---------------------------------------------------------------------
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Voice Input (PipeWire & whisper.cpp)")
        }

        QQC2.CheckBox {
            id: autoListenCheck
            Kirigami.FormData.label: i18n("Shortcut Behavior:")
            text: i18n("Automatically start listening when summoned via shortcut")
        }

        QQC2.CheckBox {
            id: vadEnabledCheck
            Kirigami.FormData.label: i18n("Voice Detection (VAD):")
            text: i18n("Automatically stop recording when speech finishes")
        }

        QQC2.SpinBox {
            id: vadSilenceSpinBox
            Kirigami.FormData.label: i18n("Silence Timeout (ms):")
            from: 400
            to: 4000
            stepSize: 100
            editable: true
            enabled: vadEnabledCheck.checked
            QQC2.ToolTip.text: i18n("Milliseconds of silence after speech before automatically ending recording")
            QQC2.ToolTip.visible: hovered
        }

        QQC2.CheckBox {
            id: wakeWordCheck
            Kirigami.FormData.label: i18n("Wake Word:")
            text: i18n("Listen for 'Oye Kiri' or 'Hey Kiri' in background")
        }

        QQC2.TextField {
            id: whisperLangField
            Kirigami.FormData.label: i18n("Recognition Language:")
            placeholderText: i18n("es (predeterminado), en, auto")
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Whisper Model Path:")
            Layout.fillWidth: true
            Layout.minimumWidth: 0

            QQC2.TextField {
                id: whisperModelField
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 0
                placeholderText: i18n("Auto-detected in ~/.local/share/kiri/models/")
            }

            QQC2.Button {
                icon.name: "document-open"
                text: i18n("Browse…")
                QQC2.ToolTip.text: i18n("Select Whisper GGML model (.bin)")
                QQC2.ToolTip.visible: hovered
                onClicked: whisperFileDialog.open()
            }
        }

        // ---------------------------------------------------------------------
        // 5. Voice Output (PipeWire & Piper TTS)
        // ---------------------------------------------------------------------
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Voice Output (Piper TTS)")
        }

        QQC2.ComboBox {
            id: speechModeCombo
            Kirigami.FormData.label: i18n("Spoken Responses:")
            model: configRoot.speechModeModel
            textRole: "text"
            valueRole: "value"
            currentIndex: {
                for (let i = 0; i < configRoot.speechModeModel.length; ++i) {
                    if (configRoot.speechModeModel[i].value === configRoot.cfg_speechOutputMode) {
                        return i;
                    }
                }
                return 0;
            }
            onActivated: index => {
                configRoot.cfg_speechOutputMode = configRoot.speechModeModel[index].value;
            }
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Piper Voice Model (.onnx):")
            Layout.fillWidth: true
            Layout.minimumWidth: 0

            QQC2.TextField {
                id: piperVoiceField
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 0
                placeholderText: i18n("Auto-detected in ~/.local/share/kiri/voices/")
            }

            QQC2.Button {
                icon.name: "document-open"
                text: i18n("Browse…")
                QQC2.ToolTip.text: i18n("Select Piper neural voice model (.onnx)")
                QQC2.ToolTip.visible: hovered
                onClicked: piperFileDialog.open()
            }
        }

        // ---------------------------------------------------------------------
        // 6. Communications & Calendar
        // ---------------------------------------------------------------------
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Communications & Calendar")
        }

        QQC2.ComboBox {
            id: defaultEmailCombo
            Kirigami.FormData.label: i18n("Default Email Client:")
            model: configRoot.emailClientModel
            textRole: "text"
            valueRole: "value"
            currentIndex: {
                for (let i = 0; i < configRoot.emailClientModel.length; ++i) {
                    if (configRoot.emailClientModel[i].value === configRoot.cfg_defaultEmailClient) {
                        return i;
                    }
                }
                return 0;
            }
            onActivated: index => {
                configRoot.cfg_defaultEmailClient = configRoot.emailClientModel[index].value;
            }
        }

        QQC2.ComboBox {
            id: defaultCalendarCombo
            Kirigami.FormData.label: i18n("Default Calendar:")
            model: configRoot.calendarClientModel
            textRole: "text"
            valueRole: "value"
            currentIndex: {
                for (let i = 0; i < configRoot.calendarClientModel.length; ++i) {
                    if (configRoot.calendarClientModel[i].value === configRoot.cfg_defaultCalendarClient) {
                        return i;
                    }
                }
                return 0;
            }
            onActivated: index => {
                configRoot.cfg_defaultCalendarClient = configRoot.calendarClientModel[index].value;
            }
        }
    }
}
