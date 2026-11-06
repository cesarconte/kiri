#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Kity contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# setup-models.sh: Download local AI, speech recognition, and TTS models for Kity

set -euo pipefail

KITY_DATA_DIR="${HOME}/.local/share/kity"
MODELS_DIR="${KITY_DATA_DIR}/models"
VOICES_DIR="${KITY_DATA_DIR}/voices"
KNOWLEDGE_DIR="${KITY_DATA_DIR}/knowledge"

echo "=========================================================="
echo "         Kity - Siri-style Voice Assistant Setup          "
echo "=========================================================="
echo ""

mkdir -p "${MODELS_DIR}"
mkdir -p "${VOICES_DIR}"
mkdir -p "${KNOWLEDGE_DIR}"

# 1. Whisper speech-to-text GGML model (Base Spanish/Multilingual)
WHISPER_MODEL="ggml-base.bin"
WHISPER_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/${WHISPER_MODEL}"

if [ ! -f "${MODELS_DIR}/${WHISPER_MODEL}" ]; then
    echo "[1/3] Descargando modelo Whisper (${WHISPER_MODEL})..."
    curl -L --progress-bar -o "${MODELS_DIR}/${WHISPER_MODEL}" "${WHISPER_URL}"
else
    echo "[1/3] Modelo Whisper ya descargado: ${MODELS_DIR}/${WHISPER_MODEL}"
fi

# 2. Piper TTS neural voice (es_ES-sharvard-medium)
PIPER_VOICE="es_ES-sharvard-medium"
PIPER_ONNX_URL="https://huggingface.co/rhasspy/piper-voices/resolve/main/es/es_ES/sharvard/medium/${PIPER_VOICE}.onnx"
PIPER_JSON_URL="https://huggingface.co/rhasspy/piper-voices/resolve/main/es/es_ES/sharvard/medium/${PIPER_VOICE}.onnx.json"

if [ ! -f "${VOICES_DIR}/${PIPER_VOICE}.onnx" ]; then
    echo "[2/3] Descargando voz neuronal Piper (${PIPER_VOICE})..."
    curl -L --progress-bar -o "${VOICES_DIR}/${PIPER_VOICE}.onnx" "${PIPER_ONNX_URL}"
    curl -L --progress-bar -o "${VOICES_DIR}/${PIPER_VOICE}.onnx.json" "${PIPER_JSON_URL}"
else
    echo "[2/3] Voz Piper ya disponible: ${VOICES_DIR}/${PIPER_VOICE}.onnx"
fi

# 3. Ollama LLM check & pull
echo "[3/3] Verificando servidor Ollama..."
if command -v ollama >/dev/null 2>&1; then
    if systemctl is-active --quiet ollama.service || pgrep -x ollama >/dev/null; then
        echo "Servidor Ollama detectado en ejecución."
        echo "Descargando modelo recomendado de lenguaje: llama3.2:3b..."
        ollama pull llama3.2:3b || echo "Aviso: no se pudo descargar llama3.2:3b automáticamente."
    else
        echo "Aviso: El servicio Ollama no está activo. Ejecuta 'systemctl start ollama' o 'ollama serve'."
    fi
else
    echo "Aviso: 'ollama' no encontrado en el PATH. Instálalo para usar el modelo LLM."
fi

echo ""
echo "¡Configuración de modelos de Kity completada exitosamente!"
