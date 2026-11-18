# Kity 🔮

> **Asistente de voz e inteligencia artificial local al estilo Siri para KDE Plasma 6 en CachyOS Linux.**

Kity combina la fluidez visual de un orbe animado estilo Siri con modelos locales de última generación:
- **LLM Local**: Inferencia mediante [Ollama](https://ollama.ai) (Llama 3.2, Mistral, Llama 3.1, Gemma 2).
- **Reconocimiento de voz (STT)**: Transcripción precisa con [whisper.cpp](https://github.com/ggerganov/whisper.cpp) y PipeWire (`pw-record`).
- **Síntesis de voz (TTS)**: Voz neuronal ultra-rápida y natural con [Piper TTS](https://github.com/rhasspy/piper) y PipeWire (`pw-play`).
- **Integración con Plasma 6 y Wayland**:
  - Lanzamiento y cierre inteligente de aplicaciones.
  - Perfiles de energía (`power-profiles-daemon`).
  - Control de notificaciones (Modo No Molestar).
  - Estado de batería (UPower).
  - Ejecución de comandos en Konsole y actualización del sistema.
  - Temporizadores y recordatorios con notificaciones nativas.
  - **Estética *Liquid Glass***: Superficies translúcidas con borde especular y refracción luminosa estilo macOS Tahoe / Golden Gate.
  - ***Writing Tools* (Apple Intelligence)**: Corrección gramatical (*Proofread*), reescritura, ajuste de tono (amigable, profesional, conciso), resúmenes, puntos clave y tablas sobre la selección primaria de Wayland o portapapeles.
  - ***Visual Intelligence & Live Text***: Captura interactiva de región (`-r`), ventana activa (`-a`) o pantalla (`-f`) con **KDE Spectacle** sobre KWin Wayland + extracción OCR con **Tesseract** + detección de enlaces/emails/teléfonos + traducción y análisis IA.
  - **Aplicación Standalone estilo macOS Golden Gate (`kity`)**: Aplicación de escritorio independiente con presencia y anclaje directo en el Dock / Gestor de tareas de Plasma (`org.cesar.kity.desktop`), ventana Liquid Glass con desenfoque KWin Wayland, historial de conversaciones sincronizado y acciones rápidas.
  - **Plugin *Spotlight* para KRunner (`krunner_kity`)**: Invoca a Kity, aplica Herramientas de Escritura (`escribe`), extrae Live Text (`ocr`) o busca semánticamente en tu base RAG directamente desde `Alt + Espacio`.
  - RAG local (Retrieval-Augmented Generation) sobre documentos en `~/.local/share/kity/knowledge/`.

---

## 🚀 Requisitos e Instalación

### Dependencias del sistema (CachyOS / Arch Linux)

```bash
sudo pacman -S extra-cmake-modules plasma-workspace qt6-declarative qt6-base pipewire spectacle tesseract tesseract-data-spa tesseract-data-eng
```

### Herramientas de Audio, Fuentes e Iconos macOS, y Modelos Locales

Kity incluye scripts para configurar automáticamente el entorno de voz (Whisper.cpp, Piper TTS), la tipografía nativa de Apple, el tema de iconos squircle de macOS y los modelos de IA:

```bash
# 1. Configurar tipografías oficiales de Apple (SF Pro, SF Mono, New York)
chmod +x scripts/setup-fonts.sh
./scripts/setup-fonts.sh

# 2. Configurar tema de iconos oficial macOS Golden Gate (WhiteSur + Breeze fallback)
chmod +x scripts/setup-icons.sh
./scripts/setup-icons.sh

# 3. Instalar o compilar herramientas de audio (Piper TTS y Whisper.cpp)
chmod +x scripts/install-audio-tools.sh
./scripts/install-audio-tools.sh

# 4. Descargar modelos locales (Whisper GGML, voz Piper y modelos Ollama)
chmod +x scripts/setup-models.sh
./scripts/setup-models.sh
```

### Compilación e Instalación

```bash
git clone --recurse-submodules https://github.com/cesarconte/kity.git
cd kity
cmake -B build -S . -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j$(nproc)
sudo cmake --install build
```

> [!IMPORTANT]
> **Política de instalación**: La instalación de Kity y todos sus componentes asociados se realiza **exclusivamente en `/usr`** para evitar sombreado (*shadowing*) de bibliotecas en Qt6/KDE Plasma y garantizar sincronización total de versiones. Nunca instalar en `~/.local`.

Para recargar el plasmoide en KDE Plasma:
```bash
plasmashell --replace &
```

---

## 🏛️ Arquitectura Modular (SRP)

Kity implementa el principio de responsabilidad única (*Single Responsibility Principle* - SRP) dividiendo sus capacidades en gestores de sistema especializados (`src/system/`) y herramientas dinámicas desacopladas (`src/tools/`) registradas en `ToolRegistry`.

Para una descripción detallada de los diagramas, contratos de herramientas, políticas Safe-by-Design y guías para extender el sistema, consulta la [Guía de Arquitectura y Blueprint](docs/ARCHITECTURE.md).

---

## 📂 Estructura del Proyecto

```
kity/
├── CMakeLists.txt              # Configuración CMake principal para Plasma 6
├── .gitignore
├── README.md
├── docs/
│   └── ARCHITECTURE.md         # Blueprint y especificación arquitectónica modular
├── scripts/
│   ├── install-audio-tools.sh  # Script para instalar/compilar Whisper.cpp y Piper TTS
│   └── setup-models.sh         # Script para descargar modelos STT y TTS
├── tests/
│   ├── CMakeLists.txt          # Suite de pruebas unitarias CTest
│   └── test_appmanager.cpp     # Pruebas de gestores del sistema y herramientas
└── src/
    ├── CMakeLists.txt          # Configuración del módulo Plasma Applet C++
    ├── metadata.json           # Metadatos del plasmoide Kity (Plasma 6)
    ├── kityapplet.h            # Orquestador del plasmoide y D-Bus
    ├── kityapplet.cpp          # Lógica central e integración QML
    ├── system/                 # Gestores de sistema independientes (SRP)
    │   ├── applicationmanager.*# Catálogo y lanzamiento Wayland de aplicaciones
    │   ├── windowmanager.*     # Gestión y cierre de ventanas en KWin
    │   ├── audiomanager.*      # PipeWire / WirePlumber
    │   ├── mediamanager.*      # MPRIS2 y playerctl
    │   ├── devicemanager.*     # Perfiles de energía y hardware
    │   ├── timermanager.*      # Temporizadores y recordatorios
    │   ├── weathermanager.*    # Previsión meteorológica Open-Meteo
    │   ├── hardwaremanager.*   # Diagnósticos y métricas del sistema
    │   ├── clipboardmanager.*  # Portapapeles Wayland (wl-clipboard)
    │   ├── terminalmanager.*   # Konsole y cachy-update nativo
    │   ├── websearchmanager.*  # Búsqueda web contextual en tiempo real
    │   ├── calendarmanager.*   # KOrganizer y Akonadi (RFC 5545)
    │   ├── emailmanager.*      # Proton Mail y Proton Mail Bridge (Safe-by-Design)
    │   └── notificationmanager.*# Resumen de notificaciones Apple Intelligence
    ├── tools/                  # Herramientas expuestas al LLM (ITool)
    │   ├── itool.h             # Interfaz base de herramienta
    │   ├── toolregistry.*      # Registro dinámico y catálogo OpenAI/Ollama
    │   └── *tool.*             # Implementaciones individuales de herramientas
    ├── qml/                    # Vistas e interfaz QML Plasma 6 (Kirigami / CachyOS.UI)
    │   ├── main.qml            # Representación compacta y acciones contextuales
    │   ├── ExpandedRepresentation.qml # Interfaz expandida del asistente
    │   ├── SiriOrbAnimation.qml# Orbe animado con visualizador reactivo
    │   └── cards/              # Tarjetas Generative UI (EmailCard, MediaCard, etc.)
    └── res/                    # Recursos XML (KConfigXT) e iconos vectoriales SVG
```

---

## 📜 Licencia

GPL-3.0-or-later.
