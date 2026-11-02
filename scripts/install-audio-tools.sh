#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Kity contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# install-audio-tools.sh: Installs Piper TTS and Whisper.cpp to ~/.local/bin if not present.

set -euo pipefail

LOCAL_BIN="${HOME}/.local/bin"
LOCAL_LIB="${HOME}/.local/lib"
mkdir -p "${LOCAL_BIN}" "${LOCAL_LIB}"

echo "=========================================================="
echo "         Kity - Audio Stack Verification & Setup          "
echo "=========================================================="

# 1. PipeWire CLI checks
echo "[1/3] Verificando herramientas PipeWire..."
if ! command -v pw-record >/dev/null 2>&1 || ! command -v pw-play >/dev/null 2>&1; then
    echo "Aviso: 'pw-record' o 'pw-play' no encontrados. Instala pipewire con: sudo pacman -S pipewire"
else
    echo "PipeWire tools OK: pw-record y pw-play disponibles."
fi

# 2. Piper TTS
echo "[2/3] Verificando Piper TTS..."
if command -v piper >/dev/null 2>&1; then
    echo "Piper TTS ya instalado en: $(which piper)"
else
    echo "Descargando Piper TTS standalone..."
    TMP_DIR=$(mktemp -d)
    PIPER_URL="https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_linux_x86_64.tar.gz"
    curl -sSL -o "${TMP_DIR}/piper.tar.gz" "${PIPER_URL}"
    tar -xzf "${TMP_DIR}/piper.tar.gz" -C "${LOCAL_LIB}/"
    ln -sf "${LOCAL_LIB}/piper/piper" "${LOCAL_BIN}/piper"
    rm -rf "${TMP_DIR}"
    echo "Piper instalado exitosamente en ${LOCAL_BIN}/piper"
fi

# 3. Whisper.cpp
echo "[3/3] Verificando Whisper.cpp..."
if command -v whisper-cli >/dev/null 2>&1; then
    echo "Whisper CLI ya instalado en: $(which whisper-cli)"
else
    echo "Compilando Whisper.cpp desde código fuente..."
    TMP_DIR=$(mktemp -d)
    git clone --depth 1 https://github.com/ggerganov/whisper.cpp.git "${TMP_DIR}/whisper.cpp"
    cmake -B "${TMP_DIR}/whisper.cpp/build" -S "${TMP_DIR}/whisper.cpp" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${HOME}/.local" \
        -DWHISPER_BUILD_EXAMPLES=ON
    cmake --build "${TMP_DIR}/whisper.cpp/build" --target whisper-cli -j$(nproc)
    cmake --install "${TMP_DIR}/whisper.cpp/build"

    mkdir -p "${LOCAL_LIB}/whisper-cpp"
    mv "${LOCAL_BIN}/whisper-cli" "${LOCAL_LIB}/whisper-cpp/whisper-cli" 2>/dev/null || true
    cat << 'EOF' > "${LOCAL_BIN}/whisper-cli"
#!/usr/bin/env bash
export LD_LIBRARY_PATH="$HOME/.local/lib:$LD_LIBRARY_PATH"
exec "$HOME/.local/lib/whisper-cpp/whisper-cli" "$@"
EOF
    chmod +x "${LOCAL_BIN}/whisper-cli"
    rm -rf "${TMP_DIR}"
    echo "Whisper.cpp instalado exitosamente en ${LOCAL_BIN}/whisper-cli"
fi

echo ""
echo "¡Herramientas de audio de Kity listas y funcionales!"
