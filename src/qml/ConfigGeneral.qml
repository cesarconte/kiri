/*
    SPDX-FileCopyrightText: 2026 Kiri contributors

    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtCore
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami

KCMUtils.SimpleKCM {
    id: configRoot

    property alias cfg_ollamaUrl: ollamaUrlField.text
    property string cfg_ollamaModel: "qwen2.5:7b"
    property var detectedModels: []

    function fetchOllamaModels() {
        const url = (ollamaUrlField.text.trim() || "http://127.0.0.1:11434") + "/api/tags";
        const xhr = new XMLHttpRequest();
        xhr.open("GET", url);
        xhr.timeout = 2500;
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
                        configRoot.detectedModels = list;
                        if (!configRoot.cfg_ollamaModel || configRoot.cfg_ollamaModel.indexOf("llama3.2") !== -1) {
                            if (list.indexOf("qwen2.5:7b") !== -1) {
                                configRoot.cfg_ollamaModel = "qwen2.5:7b";
                            } else {
                                configRoot.cfg_ollamaModel = list[0];
                            }
                        }
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
    property alias cfg_ollamaVisionModel: ollamaVisionModelField.text
    property double cfg_ollamaTemperature: 0.2
    property alias cfg_ollamaNumCtx: ollamaCtxSpinBox.value
    property alias cfg_ollamaNumThread: ollamaThreadSpinBox.value
    property alias cfg_ollamaNumPredict: ollamaPredictSpinBox.value
    property alias cfg_ollamaKeepAlive: ollamaKeepAliveField.text
    property alias cfg_knowledgeBaseEnabled: kbEnabledCheck.checked
    property alias cfg_knowledgeBaseMaxSnippets: kbSnippetsSpinBox.value
    property alias cfg_systemPrompt: systemPromptArea.text
    property alias cfg_whisperModelPath: whisperModelField.text
    property alias cfg_whisperLanguage: whisperLangField.text
    property alias cfg_piperVoicePath: piperVoiceField.text
    property string cfg_speechOutputMode: "always"
    property string cfg_defaultEmailClient: "proton"
    property string cfg_defaultCalendarClient: "proton"
    property alias cfg_autoListenOnActivate: autoListenCheck.checked
    property alias cfg_vadEnabled: vadEnabledCheck.checked
    property alias cfg_wakeWordEnabled: wakeWordCheck.checked
    property string cfg_panelIconStyle: "symbolic"

    readonly property var panelIconStyleModel: [
        {
            "value": "symbolic",
            "text": i18n("Versión simbólica (monocroma Breeze, recomendada)")
        },
        {
            "value": "color",
            "text": i18n("Versión en color (gradientes Breeze)")
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

    Kirigami.FormLayout {
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

            QQC2.ComboBox {
                id: ollamaModelCombo
                Layout.fillWidth: true
                editable: true
                model: {
                    const list = configRoot.detectedModels.slice();
                    if (configRoot.cfg_ollamaModel && list.indexOf(configRoot.cfg_ollamaModel) === -1) {
                        list.unshift(configRoot.cfg_ollamaModel);
                    }
                    if (list.length === 0) {
                        list.push("qwen2.5:7b");
                    }
                    return list;
                }
                editText: configRoot.cfg_ollamaModel
                currentIndex: {
                    const idx = model.indexOf(configRoot.cfg_ollamaModel);
                    return idx >= 0 ? idx : 0;
                }
                onEditTextChanged: {
                    if (editText.trim().length > 0) {
                        configRoot.cfg_ollamaModel = editText.trim();
                    }
                }
                onActivated: index => {
                    configRoot.cfg_ollamaModel = model[index];
                }
            }

            QQC2.Button {
                icon.name: "view-refresh"
                text: i18n("Detectar")
                QQC2.ToolTip.text: i18n("Actualizar lista de modelos instalados en Ollama")
                QQC2.ToolTip.visible: hovered
                onClicked: configRoot.fetchOllamaModels()
            }
        }

        QQC2.TextField {
            id: ollamaVisionModelField
            Kirigami.FormData.label: i18n("Vision Model:")
            placeholderText: "moondream"
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Temperature (%1):", configRoot.cfg_ollamaTemperature.toFixed(2))
            QQC2.Slider {
                id: tempSlider
                Layout.fillWidth: true
                from: 0.0
                to: 1.0
                stepSize: 0.05
                value: configRoot.cfg_ollamaTemperature
                onMoved: configRoot.cfg_ollamaTemperature = Math.round(value * 100) / 100
            }
            QQC2.Label {
                text: configRoot.cfg_ollamaTemperature <= 0.3 ? i18n("Factual (anti-alucinación)") : i18n("Creativo")
                color: Kirigami.Theme.neutralTextColor
            }
        }

        QQC2.SpinBox {
            id: ollamaCtxSpinBox
            Kirigami.FormData.label: i18n("Context Window (tokens):")
            from: 1024
            to: 32768
            stepSize: 1024
            editable: true
        }

        QQC2.SpinBox {
            id: ollamaThreadSpinBox
            Kirigami.FormData.label: i18n("CPU Inference Threads:")
            from: 1
            to: 32
            stepSize: 1
            editable: true
        }

        QQC2.SpinBox {
            id: ollamaPredictSpinBox
            Kirigami.FormData.label: i18n("Max Generated Tokens:")
            from: 64
            to: 2048
            stepSize: 64
            editable: true
        }

        QQC2.TextField {
            id: ollamaKeepAliveField
            Kirigami.FormData.label: i18n("RAM Keep-Alive:")
            placeholderText: "24h"
        }

        QQC2.TextArea {
            id: systemPromptArea
            Kirigami.FormData.label: i18n("System Prompt:")
            Layout.fillWidth: true
            implicitHeight: Kirigami.Units.gridUnit * 5
            wrapMode: TextEdit.Wrap
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Base de Conocimiento y Documentación (RAG)")
        }

        QQC2.CheckBox {
            id: kbEnabledCheck
            Kirigami.FormData.label: i18n("RAG Local:")
            text: i18n("Activar búsqueda en documentos y bibliografía local (~/.local/share/kiri/knowledge/)")
            checked: true
        }

        QQC2.SpinBox {
            id: kbSnippetsSpinBox
            Kirigami.FormData.label: i18n("Fragmentos máximos:")
            from: 1
            to: 10
            stepSize: 1
            value: 3
            editable: true
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Carpeta de Documentos:")
            Layout.fillWidth: true

            QQC2.Label {
                text: "~/.local/share/kiri/knowledge/"
                color: Kirigami.Theme.neutralTextColor
                Layout.fillWidth: true
                elide: Text.ElideMiddle
            }

            QQC2.Button {
                icon.name: "document-open-folder"
                text: i18n("Abrir en Dolphin")
                QQC2.ToolTip.text: i18n("Abrir carpeta donde colocar archivos Markdown o texto de referencia")
                QQC2.ToolTip.visible: hovered
                onClicked: {
                    Qt.openUrlExternally("file://" + StandardPaths.writableLocation(StandardPaths.GenericDataLocation) + "/kiri/knowledge");
                }
            }
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Apariencia e Icono")
        }

        QQC2.ComboBox {
            id: panelIconStyleCombo
            Kirigami.FormData.label: i18n("Icono en el panel:")
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

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Voice Input (PipeWire & whisper.cpp)")
        }

        QQC2.CheckBox {
            id: autoListenCheck
            Kirigami.FormData.label: i18n("Shortcut Behavior:")
            text: i18n("Automatically start listening when summoned via global shortcut")
        }

        QQC2.CheckBox {
            id: vadEnabledCheck
            Kirigami.FormData.label: i18n("Voice Detection (VAD):")
            text: i18n("Automatically stop recording when speech finishes")
        }

        QQC2.CheckBox {
            id: wakeWordCheck
            Kirigami.FormData.label: i18n("Wake Word:")
            text: i18n("Listen for 'Oye Kiri' or 'Hey Kiri' in background")
        }

        QQC2.TextField {
            id: whisperLangField
            Kirigami.FormData.label: i18n("Recognition Language:")
            placeholderText: i18n("auto (follows System Settings), es, or en")
        }

        QQC2.TextField {
            id: whisperModelField
            Kirigami.FormData.label: i18n("Whisper Model Path:")
            placeholderText: i18n("Auto-detected in ~/.local/share/kiri/models/")
        }

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

        QQC2.TextField {
            id: piperVoiceField
            Kirigami.FormData.label: i18n("Piper Voice Model (.onnx):")
            placeholderText: i18n("Auto-detected in ~/.local/share/kiri/voices/")
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Email & Communication")
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
