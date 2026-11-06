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
    id: root

    readonly property var speechModes: [
        { "value": "always", "text": i18n("Hablar siempre en voz alta (Recomendado)") },
        { "value": "voice_only", "text": i18n("Solo cuando preguntes por voz") },
        { "value": "manual", "text": i18n("Solo manual (al pulsar el altavoz del mensaje)") }
    ]

    readonly property var iconStyles: [
        { "value": "symbolic", "text": i18n("Simbólico monocromo (Breeze, recomendado)") },
        { "value": "color", "text": i18n("Color completo (Degradado Kity)") }
    ]

    readonly property var emailClients: [
        { "value": "proton", "text": i18n("Proton Mail (Aplicación de escritorio / Web)") },
        { "value": "gmail", "text": i18n("Google Gmail (Navegador web)") },
        { "value": "system", "text": i18n("Cliente de correo predeterminado del sistema") }
    ]

    readonly property var calendarClients: [
        { "value": "proton", "text": i18n("Proton Calendar (Aplicación / Web)") },
        { "value": "google", "text": i18n("Google Calendar (Navegador web)") }
    ]

    property bool showAdvancedOllama: false

    QtDialogs.FileDialog {
        id: whisperFileDialog
        title: i18n("Seleccionar modelo Whisper GGML (.bin)")
        nameFilters: [i18n("Modelos Whisper GGML (*.bin)"), i18n("Todos los archivos (*)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.GenericDataLocation) + "/kity/models"
        onAccepted: {
            kcm.whisperModelPath = selectedFile.toString().replace(/^file:\/\//, "");
        }
    }

    QtDialogs.FileDialog {
        id: piperFileDialog
        title: i18n("Seleccionar voz neuronal Piper (.onnx)")
        nameFilters: [i18n("Modelos Piper ONNX (*.onnx)"), i18n("Todos los archivos (*)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.GenericDataLocation) + "/kity/voices"
        onAccepted: {
            kcm.piperVoicePath = selectedFile.toString().replace(/^file:\/\//, "");
        }
    }

    Kirigami.FormLayout {
        // =====================================================================
        // 1. Voz y Escucha Activa («¡Hola Kity!»)
        // =====================================================================
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Voz y Escucha Activa («¡Hola Kity!»)")
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Activación por voz:")
            text: i18n("Escuchar «¡Hola Kity!» u «Oye Kity» en segundo plano")
            checked: kcm.wakeWordEnabled
            onToggled: kcm.wakeWordEnabled = checked
            QQC2.ToolTip.text: i18n("Permite invocar a Kity con la voz en cualquier momento de manera manos libres")
            QQC2.ToolTip.visible: hovered
        }

        QQC2.ComboBox {
            Kirigami.FormData.label: i18n("Respuestas habladas:")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            model: root.speechModes
            textRole: "text"
            valueRole: "value"
            currentIndex: {
                for (let i = 0; i < root.speechModes.length; ++i) {
                    if (root.speechModes[i].value === kcm.speechOutputMode) return i;
                }
                return 0;
            }
            onActivated: index => {
                kcm.speechOutputMode = root.speechModes[index].value;
            }
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Avisos acústicos:")
            text: i18n("Reproducir tono armónico al activar o finalizar la escucha")
            checked: kcm.earconsEnabled
            onToggled: kcm.earconsEnabled = checked
            QQC2.ToolTip.text: i18n("Emite un sonido sutil estilo Siri al comenzar y terminar la captura del micrófono")
            QQC2.ToolTip.visible: hovered
        }

        QQC2.CheckBox {
            id: vadCheck
            Kirigami.FormData.label: i18n("Detección de voz (VAD):")
            text: i18n("Detener grabación automáticamente cuando termines de hablar")
            checked: kcm.vadEnabled
            onToggled: kcm.vadEnabled = checked
        }

        QQC2.SpinBox {
            Kirigami.FormData.label: i18n("Tiempo de silencio (ms):")
            from: 400
            to: 4000
            stepSize: 100
            editable: true
            enabled: vadCheck.checked
            value: kcm.vadSilenceTimeoutMs
            onValueModified: kcm.vadSilenceTimeoutMs = value
            QQC2.ToolTip.text: i18n("Milisegundos de pausa necesarios para procesar automáticamente la orden")
            QQC2.ToolTip.visible: hovered
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Al abrir ventana:")
            text: i18n("Empezar a escuchar por micrófono inmediatamente al activar Kity")
            checked: kcm.autoListenOnActivate
            onToggled: kcm.autoListenOnActivate = checked
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Modelo Whisper (.bin):")
            Layout.fillWidth: true
            Layout.minimumWidth: 0

            QQC2.TextField {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                placeholderText: i18n("Detección automática en ~/.local/share/kity/models/")
                text: kcm.whisperModelPath
                onTextEdited: kcm.whisperModelPath = text.trim()
            }

            QQC2.Button {
                icon.name: "document-open"
                text: i18n("Examinar…")
                onClicked: whisperFileDialog.open()
            }

            Kirigami.Icon {
                source: kcm.whisperFound ? "emblem-checked" : "emblem-warning"
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
                QQC2.ToolTip.text: kcm.whisperFound ? i18n("Modelo Whisper detectado y listo") : i18n("Modelo no encontrado en la ruta indicada")
                QQC2.ToolTip.visible: whisperStatusHover.hovered
                HoverHandler { id: whisperStatusHover }
            }
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Voz Piper TTS (.onnx):")
            Layout.fillWidth: true
            Layout.minimumWidth: 0

            QQC2.TextField {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                placeholderText: i18n("Detección automática en ~/.local/share/kity/voices/")
                text: kcm.piperVoicePath
                onTextEdited: kcm.piperVoicePath = text.trim()
            }

            QQC2.Button {
                icon.name: "document-open"
                text: i18n("Examinar…")
                onClicked: piperFileDialog.open()
            }

            Kirigami.Icon {
                source: kcm.piperFound ? "emblem-checked" : "emblem-warning"
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
                QQC2.ToolTip.text: kcm.piperFound ? i18n("Voz Piper detectada y lista") : i18n("Archivo .onnx no encontrado en la ruta indicada")
                QQC2.ToolTip.visible: piperStatusHover.hovered
                HoverHandler { id: piperStatusHover }
            }
        }

        // =====================================================================
        // 2. Motor de Inteligencia Artificial (Ollama)
        // =====================================================================
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Motor de Inteligencia Artificial Local (Ollama)")
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Servidor Ollama:")
            Layout.fillWidth: true
            Layout.minimumWidth: 0

            QQC2.TextField {
                id: urlField
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                placeholderText: "http://127.0.0.1:11434"
                text: kcm.ollamaUrl
                onTextEdited: kcm.ollamaUrl = text.trim()
            }

            QQC2.Button {
                id: checkButton
                icon.name: "view-refresh-symbolic"
                text: i18n("Comprobar")
                enabled: !kcm.ollamaChecking
                QQC2.ToolTip.text: i18n("Verificar conexión con el servidor Ollama local")
                QQC2.ToolTip.visible: hovered
                onClicked: {
                    kcm.checkReadiness();
                    kcm.fetchModels();
                    refreshRotation.restart();
                }

                NumberAnimation {
                    id: refreshRotation
                    target: checkButton.icon
                    property: "rotation"
                    from: 0
                    to: 360
                    duration: 650
                    easing.type: Easing.InOutCubic
                }
            }

            Kirigami.Icon {
                source: kcm.ollamaReachable ? "emblem-checked" : "emblem-error"
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
                QQC2.ToolTip.text: kcm.ollamaReachable ? i18n("Servidor Ollama activo y accesible") : i18n("No se puede conectar a Ollama en esta dirección")
                QQC2.ToolTip.visible: checkHover.hovered
                HoverHandler { id: checkHover }
            }
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Modelo principal:")
            Layout.fillWidth: true
            Layout.minimumWidth: 0

            QQC2.ComboBox {
                id: modelCombo
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                editable: true
                model: kcm.detectedModels.length > 0 ? kcm.detectedModels : ["llama3.2:3b", "mistral:7b", "llama3.1:8b", "gemma2:9b"]
                editText: kcm.ollamaModel
                currentIndex: {
                    const idx = model.indexOf(kcm.ollamaModel);
                    return idx >= 0 ? idx : 0;
                }
                onActivated: index => {
                    kcm.ollamaModel = model[index];
                }
                onAccepted: {
                    if (editText.trim().length > 0) {
                        kcm.ollamaModel = editText.trim();
                    }
                }
                QQC2.ToolTip.text: i18n("Modelo de lenguaje local utilizado para razonar y responder")
                QQC2.ToolTip.visible: hovered
            }

            QQC2.Button {
                icon.name: "view-refresh-symbolic"
                text: i18n("Actualizar lista")
                QQC2.ToolTip.text: i18n("Consultar modelos instalados en Ollama")
                QQC2.ToolTip.visible: hovered
                onClicked: kcm.fetchModels()
            }
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Temperatura (%1):", kcm.ollamaTemperature.toFixed(2))
            Layout.fillWidth: true
            Layout.minimumWidth: 0

            QQC2.Slider {
                id: tempSlider
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                from: 0
                to: 100
                stepSize: 5
                value: Math.round(kcm.ollamaTemperature * 100)
                onMoved: kcm.ollamaTemperature = value / 100.0
            }
            QQC2.Label {
                text: kcm.ollamaTemperature <= 0.35 ? i18n("Preciso") : i18n("Creativo")
                color: Kirigami.Theme.neutralTextColor
            }
        }

        ColumnLayout {
            Kirigami.FormData.label: i18n("Instrucción base (System Prompt):")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: Kirigami.Units.smallSpacing

            QQC2.TextArea {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                implicitHeight: Kirigami.Units.gridUnit * 5
                wrapMode: TextEdit.Wrap
                text: kcm.systemPrompt
                onTextEdited: kcm.systemPrompt = text
                QQC2.ToolTip.text: i18n("Instrucciones principales de comportamiento, idioma y personalidad para Kity")
                QQC2.ToolTip.visible: hovered
            }

            QQC2.Button {
                icon.name: "edit-reset"
                text: i18n("Restaurar instrucción por defecto (Español de España)")
                onClicked: kcm.restoreDefaultPrompt()
            }
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Opciones técnicas:")
            text: i18n("Mostrar parámetros avanzados de rendimiento de Ollama")
            checked: root.showAdvancedOllama
            onToggled: root.showAdvancedOllama = checked
        }

        QQC2.SpinBox {
            visible: root.showAdvancedOllama
            Kirigami.FormData.label: i18n("Ventana de contexto (tokens):")
            from: 1024
            to: 65536
            stepSize: 1024
            editable: true
            value: kcm.ollamaNumCtx
            onValueModified: kcm.ollamaNumCtx = value
        }

        QQC2.SpinBox {
            visible: root.showAdvancedOllama
            Kirigami.FormData.label: i18n("Hilos de CPU para inferencia:")
            from: 1
            to: 64
            stepSize: 1
            editable: true
            value: kcm.ollamaNumThread
            onValueModified: kcm.ollamaNumThread = value
        }

        QQC2.SpinBox {
            visible: root.showAdvancedOllama
            Kirigami.FormData.label: i18n("Máximo tokens por respuesta:")
            from: 64
            to: 4096
            stepSize: 64
            editable: true
            value: kcm.ollamaNumPredict
            onValueModified: kcm.ollamaNumPredict = value
        }

        QQC2.TextField {
            visible: root.showAdvancedOllama
            Kirigami.FormData.label: i18n("Permanencia en RAM/VRAM:")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            placeholderText: "24h"
            text: kcm.ollamaKeepAlive
            onTextEdited: kcm.ollamaKeepAlive = text.trim()
        }

        QQC2.TextField {
            visible: root.showAdvancedOllama
            Kirigami.FormData.label: i18n("Modelo de visión (pantalla):")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            placeholderText: "moondream"
            text: kcm.ollamaVisionModel
            onTextEdited: kcm.ollamaVisionModel = text.trim()
        }

        // =====================================================================
        // 3. Base de Conocimiento Local y Privacidad (RAG)
        // =====================================================================
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Base Documental y Privacidad (RAG)")
        }

        QQC2.CheckBox {
            id: kbCheck
            Kirigami.FormData.label: i18n("Documentos locales:")
            text: i18n("Buscar en mis notas y documentos locales (.txt, .md) para enriquecer respuestas")
            checked: kcm.knowledgeBaseEnabled
            onToggled: kcm.knowledgeBaseEnabled = checked
            QQC2.ToolTip.text: i18n("Guarda apuntes privados en la carpeta para que Kity los consulte de forma 100% local")
            QQC2.ToolTip.visible: hovered
        }

        QQC2.SpinBox {
            Kirigami.FormData.label: i18n("Máximo de fragmentos a extraer:")
            from: 1
            to: 10
            stepSize: 1
            editable: true
            enabled: kbCheck.checked
            value: kcm.knowledgeBaseMaxSnippets
            onValueModified: kcm.knowledgeBaseMaxSnippets = value
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Carpeta de notas:")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            enabled: kbCheck.checked

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
                onClicked: kcm.openKnowledgeFolder()
            }
        }

        // =====================================================================
        // 4. Servicios y Productividad
        // =====================================================================
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Servicios y Productividad")
        }

        QQC2.ComboBox {
            Kirigami.FormData.label: i18n("Cliente de correo:")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            model: root.emailClients
            textRole: "text"
            valueRole: "value"
            currentIndex: {
                for (let i = 0; i < root.emailClients.length; ++i) {
                    if (root.emailClients[i].value === kcm.defaultEmailClient) return i;
                }
                return 0;
            }
            onActivated: index => {
                kcm.defaultEmailClient = root.emailClients[index].value;
            }
        }

        QQC2.ComboBox {
            Kirigami.FormData.label: i18n("Servicio de calendario:")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            model: root.calendarClients
            textRole: "text"
            valueRole: "value"
            currentIndex: {
                for (let i = 0; i < root.calendarClients.length; ++i) {
                    if (root.calendarClients[i].value === kcm.defaultCalendarClient) return i;
                }
                return 0;
            }
            onActivated: index => {
                kcm.defaultCalendarClient = root.calendarClients[index].value;
            }
        }

        // =====================================================================
        // 5. Ventana y Apariencia
        // =====================================================================
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Ventana y Apariencia")
        }

        QQC2.ComboBox {
            Kirigami.FormData.label: i18n("Estilo de icono en el panel:")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            model: root.iconStyles
            textRole: "text"
            valueRole: "value"
            currentIndex: {
                for (let i = 0; i < root.iconStyles.length; ++i) {
                    if (root.iconStyles[i].value === kcm.panelIconStyle) return i;
                }
                return 0;
            }
            onActivated: index => {
                kcm.panelIconStyle = root.iconStyles[index].value;
            }
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Comportamiento:")
            text: i18n("Mantener ventana abierta al cambiar a otra aplicación (Pin)")
            checked: kcm.pin
            onToggled: kcm.pin = checked
        }
    }
}
