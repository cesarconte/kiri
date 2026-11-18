# Reglas y Directrices de Desarrollo - Proyecto Kity

## 1. Política Estricta de Instalación: ÚNICAMENTE `/usr`

> [!IMPORTANT]
> **NUNCA instalar ni duplicar archivos en `~/.local`**.
> La instalación de Kity y todos sus componentes asociados (applet, binario `kity`, krunner, kcm, iconos, archivos `.desktop`, esquemas de servicios) **DEBE realizarse EXCLUSIVAMENTE en `/usr`**.

- **Comando de instalación autorizado**:
  ```bash
  sudo -n cmake --install build --prefix /usr
  ```
- **Prohibido**:
  - `cmake --install build --prefix ~/.local`
  - Copiar binarios, plasmoides o librerías `.so` a `~/.local/bin`, `~/.local/lib`, `~/.local/share/plasma/applets`, etc.
- **Motivo**: Duplicar en `~/.local` y `/usr` provoca sombreado (*shadowing*) de bibliotecas en Qt6/KDE, desincronización de versiones, inconsistencias en System Settings y comportamientos anómalos en `plasmashell`.

---

## 2. Estándares de Arquitectura e Ingeniería (Google / Gemini)

- **Principio de Responsabilidad Única (SRP)**:
  - Mantener `kityapplet.cpp` como un orquestador ligero (< 1.000 líneas).
  - Toda lógica especializada debe residir en sus respectivos subsistemas (`src/ai/`, `src/system/`, `src/voice/`, `src/tools/`).
- **Verificación**:
  - Siempre verificar con la suite de pruebas `ctest` tras cada cambio.
  - Asegurar 100% de tests aprobados sin regresiones.
