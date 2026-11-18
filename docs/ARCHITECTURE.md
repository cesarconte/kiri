# Arquitectura y Blueprint Modular de Kity 🔮
> **Ecosistema CachyOS Linux | KDE Plasma 6 (Wayland) | Apple Intelligence & Golden Gate Design**

---

## 1. Visión General y Objetivos Arquitectónicos

Kity nació como un plasmoide y asistente de inteligencia artificial para KDE Plasma 6. Con el crecimiento orgánico de sus capacidades (STT, TTS, LLMs locales, visión computacional, RAG, control de ventanas, multimedia, calendario, correo y notificaciones), el núcleo backend (`src/kityapplet.cpp`) llegó a concentrar más de **7.140 líneas de código monolítico**, violando el principio de responsabilidad única (*Single Responsibility Principle* - SRP) y dificultando el mantenimiento, las pruebas unitarias y la extensibilidad.

Esta refactorización modular desacopla la lógica del sistema en dos capas especializadas e independientes:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        Capa de Presentación QML                        │
│   • Cápsula Rápida (Pill)  • App Dedicada (Desktop)  • Generative UI   │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ Q_PROPERTY / Q_INVOKABLE / D-Bus
┌───────────────────────────────────▼────────────────────────────────────┐
│                    Orquestador Núcleo (KityApplet)                     │
│           • Máquina de Estados  • STT/TTS  • Inferencia LLM            │
└───────────────────┬────────────────────────────────┬───────────────────┘
                    │                                │
    ┌───────────────▼──────────────┐   ┌─────────────▼───────────────┐
    │  Registro de Herramientas     │   │     Gestores de Sistema     │
    │        (ToolRegistry)        │   │       (src/system/)         │
    │  • Esquemas JSON Function    │   │  • Singletons C++           │
    │  • Despacho Dinámico         │   │  • Servicios nativos Plasma │
    │  • Filtros por capacidad     │   │  • Acceso a hardware y apps │
    └───────────────┬──────────────┘   └─────────────▲───────────────┘
                    │                                │
                    └─────── Ejecuta ITool ──────────┘
```

---

## 2. Capa de Herramientas (`src/tools/`)

### 2.1. Interfaz Base `ITool` (`src/tools/itool.h`)
Cada capacidad expuesta al modelo de lenguaje (Ollama / Llama / Mistral / Qwen) implementa el contrato `ITool`:

```cpp
namespace Kity {

class ITool {
public:
    virtual ~ITool() = default;
    virtual QString name() const = 0;
    virtual QString description() const = 0;
    virtual QJsonObject parameters() const = 0;
    virtual bool isLightModelCompatible() const { return true; }
    virtual QJsonObject execute(const QJsonObject &arguments) = 0;
};

} // namespace Kity
```

### 2.2. Registro Dinámico `ToolRegistry` (`src/tools/toolregistry.h/.cpp`)
Centraliza el registro y ciclo de vida de herramientas:
- **`registerDefaultTools()`**: Registra las 12 herramientas nativas al inicializarse.
- **`getToolDefinitions(bool isLightModel, const QString &query)`**: Genera el catálogo JSON Schema compatible con OpenAI/Ollama function calling. Permite podar herramientas complejas en modelos ligeros (p. ej. Llama 3.2 1B/3B).
- **`executeTool(const QString &name, const QJsonObject &args)`**: Despacha la ejecución a la herramienta correspondiente con control de errores integrado.

### 2.3. Catálogo de Herramientas Registradas

| Herramienta | Función LLM | Gestor Asociado | Propósito |
| :--- | :--- | :--- | :--- |
| `LaunchAppTool` | `launch_application` | `ApplicationManager` | Lanzar aplicaciones instaladas mediante tokens Wayland |
| `CloseAppTool` | `close_application` | `WindowManager` / `ApplicationManager` | Cerrar aplicaciones o ventanas activas en KWin |
| `MediaControlTool` | `media_control` | `MediaManager` | Control MPRIS2 (`play`, `pause`, `next`, `previous`) |
| `TimerTool` | `set_timer` | `TimerManager` | Programar temporizadores con cuenta regresiva y voz |
| `WeatherTool` | `get_weather` | `WeatherManager` | Previsión meteorológica local y por ciudad |
| `SystemVitalsTool` | `get_system_vitals` | `HardwareManager` | Diagnóstico de CPU, GPU, RAM, disco y batería |
| `ClipboardTool` | `clipboard_tool` | `ClipboardManager` | Leer y escribir en el portapapeles de Wayland |
| `TerminalCommandTool` | `run_terminal_command` | `TerminalManager` | Ejecución en Konsole (con integración `cachy-update`) |
| `WebSearchTool` | `web_search` | `WebSearchManager` | Búsqueda en la web en tiempo real |
| `CalendarEventTool` | `create_calendar_event` | `CalendarManager` | Creación de citas RFC 5545 en KOrganizer ("César Conte") |
| `EmailTool` | `email_tool` | `EmailManager` | Redacción y envío seguro de correo en Proton Mail |
| `NotificationTool` | `summarize_notifications` | `NotificationManager` | Resumen ejecutivo y priorización de alertas (estilo Apple) |

---

## 3. Gestores del Sistema (`src/system/`)

Cada gestor es un *Singleton* de responsabilidad única que aísla la comunicación con las APIs de Linux, KDE Frameworks 6, Qt 6 y D-Bus.

### 3.1. `ApplicationManager` & `WindowManager`
* **`ApplicationManager`**: Escanea archivos `.desktop` en `$XDG_DATA_DIRS`, mantiene un catálogo en memoria con nombres localizados, ejecutables y categorías. Lanza aplicaciones con `KIO::ApplicationLauncherJob` preservando el token de activación XDG Wayland.
* **`WindowManager`**: Interactúa con KWin Wayland para listar ventanas activas, enfocar ventanas y cerrar aplicaciones por PID o clase de ventana de manera limpia (`SIGTERM`), evitando bloqueos de interfaz.

### 3.2. `AudioManager` & `MediaManager`
* **`AudioManager`**: Controla sinks y sources de PipeWire / WirePlumber (`wpctl`), gestionando volumen maestro, silenciado de micrófono y perfiles de audio.
* **`MediaManager`**: Implementa cliente MPRIS2 sobre D-Bus / `playerctl`. Devuelve metadatos enriquecidos (artista, título, álbum, portada `mpris:artUrl`) para alimentar la tarjeta interactiva `MediaCard.qml`.

### 3.3. `DeviceManager` & `HardwareManager`
* **`DeviceManager`**: Administra perfiles de energía (`power-profiles-daemon`), brillo de pantalla (`brightnessctl`) y conectividad.
* **`HardwareManager`**: Lee métricas del kernel (`/proc/stat`, `/proc/meminfo`, `sysfs`, `nvidia-smi` o `amdgpu`) y UPower para proporcionar diagnósticos de hardware comprensibles para el usuario.

### 3.4. `TimerManager`
* Mantiene temporizadores activos con `QTimer` en hilo seguro.
* Emite señales `timerFinished` y `reminderTriggered` conectadas a la síntesis de voz (Piper TTS) y al daemon de notificaciones nativo de Plasma.
* Almacena recordatorios persistentes en formato JSON.

### 3.5. `WeatherManager` & `WebSearchManager`
* **`WeatherManager`**: Consulta APIs meteorológicas sin claves externas (Open-Meteo), formateando la respuesta con temperatura, sensación térmica, viento y código meteorológico WMO.
* **`WebSearchManager`**: Realiza búsquedas web contextuales extrayendo resúmenes de fuentes fiables para alimentar el prompt aumentado (RAG en vivo) del LLM.

### 3.6. `TerminalManager` (CachyOS Native)
* Diseñado específicamente para el ecosistema CachyOS / Arch Linux.
* Si el comando solicitado es de actualización del sistema, ejecuta de forma canónica **`cachy-update`** en una ventana flotante de Konsole.
* Cierre automático de la terminal tras el éxito de la tarea sin requerir intervención manual innecesaria.

### 3.7. `CalendarManager` (Ecosistema KOrganizer / Akonadi)
* Soporte nativo para el calendario de eventos predeterminado: **César Conte** (sincronizado con Google Calendar vía Akonadi `akonadi_google_resource_0`).
* Genera especificaciones RFC 5545 (`.ics`) con `X-WR-CALNAME:César Conte`.
* Invoca KOrganizer (`korganizer -m`) y dispara la sincronización D-Bus de Akonadi para reflejar citas inmediatamente en todos los dispositivos.

### 3.8. `EmailManager` (Proton Mail & Proton Mail Bridge)
* Integración con la aplicación oficial de escritorio de **Proton Mail** (`/usr/bin/proton-mail`) y el servicio **Proton Mail Bridge** (`127.0.0.1:1025` SMTP / `127.0.0.1:1143` IMAP).
* **Filosofía Safe-by-Design (Human-in-the-Loop)**:
  1. Kity **nunca** envía correos a ciegas.
  2. Extrae el mensaje de forma semántica con `parsePrompt()` (dando prioridad al texto entrecomillado y limpiando frases conversacionales como *"con el siguiente texto:"*).
  3. Prepara un borrador en estado `awaiting_confirmation` con refinamiento de tono profesional y cortés.
  4. Muestra la tarjeta `EmailCard.qml` en la interfaz de Kity.
  5. El usuario puede pulsar **Enviar** (o pedírselo a Kity mediante voz/chat), **Ver en Proton** o **Descartar**.
  6. Al descartar o enviar, Kity elimina el borrador y cierra automáticamente la ventana de Proton Mail mediante `WindowManager`.

### 3.9. `NotificationManager` (Apple Intelligence Style)
* Monitoriza el registro del sistema mediante `journalctl --user -p 4..0` en busca de alertas prioritarias, avisos de hardware y errores de servicios en las últimas horas.
* Consolida eventos con recordatorios y temporizadores del sistema.
* Genera síntesis ejecutivas estructuradas y envía notificaciones nativas con `KNotification`.

---

## 4. Interfaz Generativa (*Generative UI Cards*)

Ubicadas en `src/qml/cards/` y `src/app/qml/cards/`, renderizan información viva en lugar de texto plano:

1. **`WeatherCard.qml`**: Estado del tiempo, iconos de condiciones, temperaturas extremas.
2. **`MediaCard.qml`**: Reproductor interactivo con controles de pista y carátula del álbum.
3. **`EventCard.qml`**: Detalles del evento del calendario, hora y enlace a KOrganizer.
4. **`EmailCard.qml`**:
   - Construida estrictamente con **`CachyOS.UI`** (`CachyTheme`, `CachyPalette`).
   - Diseño adaptable (*responsive*): en la vista compacta/píldora (`width < 410px`), el botón principal de envío toma una fila dedicada completa para evitar cualquier desbordamiento visual.
   - Indicador de estado de alto contraste: Badge ámbar Golden Gate (`#FFD60A` / `#92400E`) para garantizar legibilidad perfecta sobre temas oscuros o claros.

---

## 5. Guía para Extender la Arquitectura

Para añadir una nueva capacidad al ecosistema de Kity:

### Paso 1: Crear el Gestor en `src/system/`
```cpp
// src/system/ejemplomanager.h
#pragma once
#include <QObject>

namespace Kity {

class EjemploManager : public QObject {
    Q_OBJECT
public:
    static EjemploManager &instance();
    bool realizarAccion(const QString &param);
private:
    explicit EjemploManager(QObject *parent = nullptr);
};

} // namespace Kity
```

### Paso 2: Crear la Herramienta en `src/tools/`
```cpp
// src/tools/ejemplotool.h
#pragma once
#include "itool.h"

namespace Kity {

class EjemploTool : public ITool {
public:
    QString name() const override { return QStringLiteral("ejemplo_tool"); }
    QString description() const override { return QStringLiteral("Descripción para el LLM."); }
    QJsonObject parameters() const override;
    QJsonObject execute(const QJsonObject &arguments) override;
};

} // namespace Kity
```

### Paso 3: Registrar en `ToolRegistry`
En `src/tools/toolregistry.cpp`:
```cpp
#include "ejemplotool.h"
// ...
void ToolRegistry::registerDefaultTools() {
    // ...
    registerTool(std::make_shared<EjemploTool>());
}
```

### Paso 4: Añadir Pruebas Unitarias
En `tests/test_appmanager.cpp`, crear un método `testEjemploManagerAndTool()` validando la lógica del gestor y la ejecución de la herramienta con `ToolRegistry::instance().executeTool()`.

---

## 6. Verificación y Suite de Pruebas

Toda la arquitectura modular cuenta con pruebas unitarias continuas en `tests/test_appmanager.cpp`:

```bash
# Compilación completa del proyecto
cmake --build build -j$(nproc)

# Ejecución de la suite completa de pruebas
ctest --test-dir build --output-on-failure

# Salida esperada:
# 1/7 Test #1: appstreamtest ....................   Passed
# 2/7 Test #2: appstreamtest ....................   Passed
# 3/7 Test #3: test_wakeword ....................   Passed
# 4/7 Test #4: test_mathnotes ..................   Passed
# 5/7 Test #5: test_mobileserver ................   Passed
# 6/7 Test #6: test_kdictationclient ............   Passed
# 7/7 Test #7: test_appmanager ..................   Passed
# 100% tests passed out of 7
```
