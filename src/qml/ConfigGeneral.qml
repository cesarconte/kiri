/*
    SPDX-FileCopyrightText: 2026 Kity contributors
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

    // Voice & Audio (PipeWire, whisper.cpp, Piper TTS)
    property alias cfg_wakeWordEnabled: wakeWordCheck.checked
    property string cfg_speechOutputMode: "always"
    property alias cfg_earconsEnabled: earconsCheck.checked
    property alias cfg_vadEnabled: vadEnabledCheck.checked
    property alias cfg_vadSilenceTimeoutMs: vadSilenceSpinBox.value
    property alias cfg_autoListenOnActivate: autoListenCheck.checked
    property string cfg_whisperLanguage: "es"
    property alias cfg_whisperModelPath: whisperModelField.text
    property alias cfg_piperVoicePath: piperVoiceField.text

    // Ollama AI Engine
    property alias cfg_ollamaUrl: ollamaUrlField.text
    property string cfg_ollamaModel: "qwen2.5:7b"
    property string cfg_ollamaVisionModel: "moondream"
    property double cfg_ollamaTemperature: 0.2
    property alias cfg_ollamaNumCtx: ollamaCtxSpinBox.value
    property alias cfg_ollamaNumThread: ollamaThreadSpinBox.value
    property alias cfg_ollamaNumPredict: ollamaPredictSpinBox.value
    property alias cfg_ollamaKeepAlive: ollamaKeepAliveField.text
    property alias cfg_systemPrompt: systemPromptArea.text

    // Local RAG Knowledge Base
    property alias cfg_knowledgeBaseEnabled: kbEnabledCheck.checked
    property alias cfg_knowledgeBaseMaxSnippets: kbSnippetsSpinBox.value

    // Appearance & Services
    property string cfg_panelIconStyle: "symbolic"
    property alias cfg_pin: pinWindowCheck.checked
    property string cfg_defaultEmailClient: "proton"
    property string cfg_defaultCalendarClient: "proton"

    // =========================================================================
    // Local State & Dynamic Ollama Model Discovery
    // =========================================================================

    property var detectedModels: ["qwen2.5:7b"]
    property bool showAdvancedEngine: false
    readonly property string defaultSpanishPrompt: "Eres Kity, un asistente de voz e inteligencia artificial nativo para KDE Plasma en CachyOS Linux. Responde SIEMPRE en español de España (castellano), de forma concisa, clara, directa y natural, optimizada para síntesis de voz (TTS)."

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
                            const name = data.models[i].name || "";
                            // Exclude vision-only auxiliary models from main chat LLM selector
                            if (name.length > 0 && name.toLowerCase().indexOf("moondream") === -1) {
                                list.push(name);
                            }
                        }
                    }
                    if (list.length > 0) {
                        if (configRoot.cfg_ollamaModel
                            && configRoot.cfg_ollamaModel.toLowerCase().indexOf("moondream") === -1
                            && list.indexOf(configRoot.cfg_ollamaModel) === -1) {
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

    readonly property var speechModeModel: [
        {
            "value": "always",
            "text": i18n("Hablar siempre en voz alta (Recomendado)")
        },
        {
            "value": "voice_only",
            "text": i18n("Solo cuando preguntes con la voz")
        },
        {
            "value": "manual",
            "text": i18n("Solo manual (al pulsar el altavoz en un mensaje)")
        }
    ]

    readonly property var panelIconStyleModel: [
        {
            "value": "symbolic",
            "text": i18n("Simbólico monocromo (Breeze, recomendado)")
        },
        {
            "value": "color",
            "text": i18n("Color completo (Degradado Breeze)")
        }
    ]

    readonly property var emailClientModel: [
        {
            "value": "proton",
            "text": i18n("Proton Mail (Aplicación de escritorio)")
        },
        {
            "value": "gmail",
            "text": i18n("Google Gmail (Navegador web)")
        },
        {
            "value": "system",
            "text": i18n("Cliente de correo predeterminado del sistema")
        }
    ]

    readonly property var calendarClientModel: [
        {
            "value": "proton",
            "text": i18n("Proton Calendar (Aplicación / Web)")
        },
        {
            "value": "google",
            "text": i18n("Google Calendar (Navegador web)")
        }
    ]

    // =========================================================================
    // File Selection Dialogs
    // =========================================================================

    QtDialogs.FileDialog {
        id: whisperFileDialog
        title: i18n("Seleccionar modelo Whisper GGML (.bin)")
        nameFilters: [i18n("Modelos Whisper GGML (*.bin)"), i18n("Todos los archivos (*)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.GenericDataLocation) + "/kity/models"
        onAccepted: {
            whisperModelField.text = selectedFile.toString().replace(/^file:\/\//, "");
        }
    }

    QtDialogs.FileDialog {
        id: piperFileDialog
        title: i18n("Seleccionar modelo de voz Piper (.onnx)")
        nameFilters: [i18n("Modelos Piper ONNX (*.onnx)"), i18n("Todos los archivos (*)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.GenericDataLocation) + "/kity/voices"
        onAccepted: {
            piperVoiceField.text = selectedFile.toString().replace(/^file:\/\//, "");
        }
    }

    // =========================================================================
    // Form Layout
    // =========================================================================

    Kirigami.FormLayout {
        // ---------------------------------------------------------------------
        // 1. Voice & Hands-Free Wake Word ("¡Hola Kity!")
        // ---------------------------------------------------------------------
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Voz y Escucha Activa («¡Hola Kity!»)")
        }

        QQC2.CheckBox {
            id: wakeWordCheck
            Kirigami.FormData.label: i18n("Activación por voz:")
            text: i18n("Escuchar «¡Hola Kity!» u «Oye Kity» en segundo plano")
            QQC2.ToolTip.text: i18n("Permite invocar a Kity con la voz en cualquier momento sin usar el ratón ni el teclado")
            QQC2.ToolTip.visible: hovered
        }

        QQC2.ComboBox {
            id: speechModeCombo
            Kirigami.FormData.label: i18n("Respuestas habladas:")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 0
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

        QQC2.CheckBox {
            id: earconsCheck
            Kirigami.FormData.label: i18n("Avisos acústicos:")
            text: i18n("Reproducir tono armónico al activar o finalizar el micrófono")
            QQC2.ToolTip.text: i18n("Emite un sonido sutil al estilo Siri cuando Kity empieza o termina de escucharte")
            QQC2.ToolTip.visible: hovered
        }

        QQC2.CheckBox {
            id: vadEnabledCheck
            Kirigami.FormData.label: i18n("Detección de silencio:")
            text: i18n("Detener grabación automáticamente cuando termines de hablar")
        }

        QQC2.SpinBox {
            id: vadSilenceSpinBox
            Kirigami.FormData.label: i18n("Tiempo de silencio (ms):")
            from: 400
            to: 4000
            stepSize: 100
            editable: true
            enabled: vadEnabledCheck.checked
            QQC2.ToolTip.text: i18n("Milisegundos de pausa tras hablar antes de procesar automáticamente tu petición")
            QQC2.ToolTip.visible: hovered
        }

        QQC2.CheckBox {
            id: autoListenCheck
            Kirigami.FormData.label: i18n("Al abrir el popup:")
            text: i18n("Empezar a escuchar por micrófono inmediatamente al abrir Kity")
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Modelo Whisper (.bin):")
            Layout.fillWidth: true
            Layout.minimumWidth: 0

            QQC2.TextField {
                id: whisperModelField
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 0
                placeholderText: i18n("Detección automática en ~/.local/share/kity/models/")
            }

            QQC2.Button {
                icon.name: "document-open"
                text: i18n("Examinar…")
                QQC2.ToolTip.text: i18n("Seleccionar archivo de modelo Whisper GGML (.bin)")
                QQC2.ToolTip.visible: hovered
                onClicked: whisperFileDialog.open()
            }
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Voz Piper TTS (.onnx):")
            Layout.fillWidth: true
            Layout.minimumWidth: 0

            QQC2.TextField {
                id: piperVoiceField
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 0
                placeholderText: i18n("Detección automática en ~/.local/share/kity/voices/")
            }

            QQC2.Button {
                icon.name: "document-open"
                text: i18n("Examinar…")
                QQC2.ToolTip.text: i18n("Seleccionar archivo de voz neuronal Piper (.onnx)")
                QQC2.ToolTip.visible: hovered
                onClicked: piperFileDialog.open()
            }
        }

        // ---------------------------------------------------------------------
        // 2. Ollama AI Engine Section
        // ---------------------------------------------------------------------
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Motor de Inteligencia Artificial (Ollama)")
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Modelo principal:")
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
                QQC2.ToolTip.text: i18n("Modelo de lenguaje en Ollama utilizado para razonar y conversar en español")
                QQC2.ToolTip.visible: hovered
            }

            QQC2.Button {
                icon.name: "view-refresh-symbolic"
                text: i18n("Detectar")
                QQC2.ToolTip.text: i18n("Actualizar lista de modelos instalados en el servidor Ollama")
                QQC2.ToolTip.visible: hovered
                onClicked: configRoot.fetchOllamaModels()
            }
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Temperatura (%1):", configRoot.cfg_ollamaTemperature.toFixed(2))
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
                text: configRoot.cfg_ollamaTemperature <= 0.3 ? i18n("Preciso") : i18n("Creativo")
                color: Kirigami.Theme.neutralTextColor
            }
        }

        ColumnLayout {
            Kirigami.FormData.label: i18n("Instrucción base:")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: Kirigami.Units.smallSpacing

            QQC2.TextArea {
                id: systemPromptArea
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 0
                implicitHeight: Kirigami.Units.gridUnit * 4.5
                wrapMode: TextEdit.Wrap
                QQC2.ToolTip.text: i18n("Personalidad, idioma y reglas principales del asistente")
                QQC2.ToolTip.visible: hovered
            }

            QQC2.Button {
                icon.name: "edit-reset"
                text: i18n("Restaurar instrucción por defecto (Español de España)")
                onClicked: {
                    systemPromptArea.text = configRoot.defaultSpanishPrompt;
                }
            }
        }

        QQC2.CheckBox {
            id: advancedEngineCheck
            Kirigami.FormData.label: i18n("Opciones técnicas:")
            text: i18n("Mostrar parámetros avanzados de rendimiento de Ollama")
            checked: configRoot.showAdvancedEngine
            onToggled: configRoot.showAdvancedEngine = checked
        }

        QQC2.TextField {
            id: ollamaUrlField
            visible: configRoot.showAdvancedEngine
            Kirigami.FormData.label: i18n("URL de Ollama:")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 0
            placeholderText: "http://127.0.0.1:11434"
        }

        QQC2.SpinBox {
            id: ollamaCtxSpinBox
            visible: configRoot.showAdvancedEngine
            Kirigami.FormData.label: i18n("Ventana de contexto (tokens):")
            from: 1024
            to: 65536
            stepSize: 1024
            editable: true
        }

        QQC2.SpinBox {
            id: ollamaThreadSpinBox
            visible: configRoot.showAdvancedEngine
            Kirigami.FormData.label: i18n("Hilos de CPU:")
            from: 1
            to: 64
            stepSize: 1
            editable: true
        }

        QQC2.SpinBox {
            id: ollamaPredictSpinBox
            visible: configRoot.showAdvancedEngine
            Kirigami.FormData.label: i18n("Máximo de tokens por respuesta:")
            from: 64
            to: 4096
            stepSize: 64
            editable: true
        }

        QQC2.TextField {
            id: ollamaKeepAliveField
            visible: configRoot.showAdvancedEngine
            Kirigami.FormData.label: i18n("Permanencia en RAM/VRAM:")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 0
            placeholderText: "24h"
        }

        // ---------------------------------------------------------------------
        // 3. Local Knowledge Base (RAG)
        // ---------------------------------------------------------------------
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Base de Conocimiento Local (RAG)")
        }

        QQC2.CheckBox {
            id: kbEnabledCheck
            Kirigami.FormData.label: i18n("Documentos locales:")
            text: i18n("Buscar en mis notas locales (.txt / .md) para enriquecer respuestas")
            checked: true
            QQC2.ToolTip.text: i18n("Guarda apuntes o documentos en ~/.local/share/kity/knowledge/ para que Kity los consulte de forma privada")
            QQC2.ToolTip.visible: hovered
        }

        QQC2.SpinBox {
            id: kbSnippetsSpinBox
            Kirigami.FormData.label: i18n("Máximo de fragmentos:")
            from: 1
            to: 10
            stepSize: 1
            value: 3
            editable: true
            enabled: kbEnabledCheck.checked
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Carpeta de notas:")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            enabled: kbEnabledCheck.checked

            QQC2.Label {
                text: "~/.local/share/kity/knowledge/"
                font.family: "monospace"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideMiddle
            }

            QQC2.Button {
                icon.name: "document-open-folder"
                text: i18n("Abrir en Dolphin")
                QQC2.ToolTip.text: i18n("Abrir la carpeta de conocimiento local en el gestor de archivos Dolphin")
                QQC2.ToolTip.visible: hovered
                onClicked: {
                    const path = StandardPaths.writableLocation(StandardPaths.GenericDataLocation) + "/kity/knowledge";
                    Qt.openUrlExternally("file://" + path);
                }
            }
        }

        // ---------------------------------------------------------------------
        // 4. Appearance & Services
        // ---------------------------------------------------------------------
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Apariencia y Servicios")
        }

        QQC2.ComboBox {
            id: panelIconStyleCombo
            Kirigami.FormData.label: i18n("Icono en el panel:")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 0
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
            Kirigami.FormData.label: i18n("Comportamiento:")
            text: i18n("Mantener ventana abierta al cambiar a otra aplicación (Pin)")
        }

        QQC2.ComboBox {
            id: defaultEmailCombo
            Kirigami.FormData.label: i18n("Cliente de correo:")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 0
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
            Kirigami.FormData.label: i18n("Calendario:")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 0
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
