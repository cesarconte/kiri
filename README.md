# Kiri 🔮

> **Asistente de voz e inteligencia artificial local al estilo Siri para KDE Plasma 6 en CachyOS Linux.**

Kiri combina la fluidez visual de un orbe animado estilo Siri con modelos locales de última generación:
- **LLM Local**: Inferencia mediante [Ollama](https://ollama.ai) (Qwen 2.5, LLaVA, Moondream).
- **Reconocimiento de voz (STT)**: Transcripción precisa con [whisper.cpp](https://github.com/ggerganov/whisper.cpp) y PipeWire (`pw-record`).
- **Síntesis de voz (TTS)**: Voz neuronal ultra-rápida y natural con [Piper TTS](https://github.com/rhasspy/piper) y PipeWire (`pw-play`).
- **Integración con Plasma 6 y Wayland**:
  - Lanzamiento y cierre inteligente de aplicaciones.
  - Perfiles de energía (`power-profiles-daemon`).
  - Control de notificaciones (Modo No Molestar).
  - Estado de batería (UPower).
  - Ejecución de comandos en Konsole y actualización del sistema.
  - Temporizadores y recordatorios con notificaciones nativas.
  - Visión de pantalla en tiempo real.
  - RAG local (Retrieval-Augmented Generation) sobre documentos en `~/.local/share/kiri/knowledge/`.

---

## 🚀 Requisitos e Instalación

### Dependencias del sistema (CachyOS / Arch Linux)

```bash
sudo pacman -S extra-cmake-modules plasma-workspace qt6-declarative qt6-base pipewire grim
```

### Herramientas de Audio y Modelos Locales

Kiri incluye scripts para configurar automáticamente el entorno de voz (Whisper.cpp, Piper TTS) y los modelos de IA:

```bash
# 1. Instalar o compilar herramientas de audio (Piper TTS y Whisper.cpp)
chmod +x scripts/install-audio-tools.sh
./scripts/install-audio-tools.sh

# 2. Descargar modelos locales (Whisper GGML, voz Piper y modelos Ollama)
chmod +x scripts/setup-models.sh
./scripts/setup-models.sh
```

### Compilación e Instalación

```bash
cmake -B build -S . -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build
cmake --install build
```

Para recargar el plasmoide en KDE Plasma:
```bash
plasmashell --replace &
```

---

## 📂 Estructura del Proyecto

```
kiri/
├── CMakeLists.txt              # Configuración CMake principal para Plasma 6
├── .gitignore
├── README.md
├── scripts/
│   ├── install-audio-tools.sh  # Script para instalar/compilar Whisper.cpp y Piper TTS
│   └── setup-models.sh         # Script para descargar modelos STT y TTS
└── src/
    ├── CMakeLists.txt          # Configuración del módulo Plasma Applet C++
    ├── metadata.json           # Metadatos del plasmoide Kiri (Plasma 6)
    ├── kiriapplet.h            # Cabecera principal del plasmoide
    ├── kiriapplet.cpp          # Lógica backend C++
    ├── qml/                    # Vistas e interfaz QML Plasma 6 (Kirigami)
    │   ├── main.qml            # Representación compacta y acciones contextuales
    │   ├── ExpandedRepresentation.qml # Interfaz expandida del asistente
    │   ├── SiriOrbAnimation.qml# Orbe animado con visualizador reactivo
    │   ├── config.qml          # Configuración del plasmoide
    │   └── ConfigGeneral.qml   # Ajustes de Ollama, Whisper, Piper y atajos
    └── res/                    # Recursos XML (KConfigXT) e iconos vectoriales SVG
        ├── main.xml
        └── *.svg
```

---

## 📜 Licencia

GPL-3.0-or-later.
