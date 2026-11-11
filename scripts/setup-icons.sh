#!/usr/bin/env bash
# ==============================================================================
# setup-icons.sh - Aprovisionamiento del tema de iconos macOS Golden Gate
# ==============================================================================
# Instala e integra el tema de iconos hiperrealista estilo macOS (squircle)
# con compatibilidad exhaustiva para KDE Plasma 6 y fallback total en Breeze.
# ==============================================================================

set -euo pipefail

ICONS_DIR="${HOME}/.local/share/icons"
REPO_URL="https://github.com/cesarconte/macos-goldengate-icons.git"
TEMP_CLONE_DIR="/tmp/macos-goldengate-icons-build"

echo "==> [1/5] Descargando y compilando el tema de iconos macOS (WhiteSur)..."
mkdir -p "${ICONS_DIR}"

if [ ! -d "${TEMP_CLONE_DIR}" ]; then
    git clone --depth 1 "${REPO_URL}" "${TEMP_CLONE_DIR}"
else
    git -C "${TEMP_CLONE_DIR}" pull --rebase || true
fi

echo "==> [2/5] Instalando variantes en ${ICONS_DIR}..."
"${TEMP_CLONE_DIR}/install.sh" -d "${ICONS_DIR}"

echo "==> [3/5] Configurando jerarquía estricta de herencia (Breeze fallback)..."
sed -i 's/^Inherits=.*/Inherits=breeze,breeze-dark,hicolor/' "${ICONS_DIR}/WhiteSur/index.theme"
sed -i 's/^Inherits=.*/Inherits=breeze,breeze-dark,hicolor/' "${ICONS_DIR}/WhiteSur-light/index.theme"
sed -i 's/^Inherits=.*/Inherits=breeze-dark,breeze,hicolor/' "${ICONS_DIR}/WhiteSur-dark/index.theme"

echo "==> [4/5] Enlazando aplicaciones del ecosistema CachyOS y Launchpad..."
for theme in WhiteSur WhiteSur-light WhiteSur-dark macOS-GoldenGate macOS-GoldenGate-light macOS-GoldenGate-dark; do
    APPS_DIR="${ICONS_DIR}/${theme}/apps/scalable"
    if [ -d "${APPS_DIR}" ]; then
        if [ -f "${ICONS_DIR}/launchpad.svg" ]; then
            cp -f "${ICONS_DIR}/launchpad.svg" "${APPS_DIR}/launchpad.svg" 2>/dev/null || true
        fi
        ln -sf brave.svg "${APPS_DIR}/brave-origin.svg" 2>/dev/null || true
        ln -sf org.cachyos.hello.svg "${APPS_DIR}/cachyos-hello.svg" 2>/dev/null || true
        ln -sf cachyos-kernel-manager.svg "${APPS_DIR}/org.cachyos.KernelManager.svg" 2>/dev/null || true
        ln -sf system-software-update.svg "${APPS_DIR}/LimineSnapperSync.svg" 2>/dev/null || true
        ln -sf system-software-update.svg "${APPS_DIR}/cachy-update-blue.svg" 2>/dev/null || true
        ln -sf system-software-update.svg "${APPS_DIR}/cachy-update_updates-available-blue.svg" 2>/dev/null || true

        # Integrar iconos de Kity (tanto a color como simbólicos para panel de Plasma)
        SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
        RES_DIR="${SCRIPT_DIR}/../src/res"
        if [ -d "${RES_DIR}" ]; then
            mkdir -p "${ICONS_DIR}/${theme}/apps/symbolic" "${ICONS_DIR}/${theme}/status/symbolic"
            cp -f "${RES_DIR}/kity-symbolic.svg" "${ICONS_DIR}/${theme}/apps/symbolic/kity-symbolic.svg" 2>/dev/null || true
            cp -f "${RES_DIR}/kity-symbolic.svg" "${ICONS_DIR}/${theme}/status/symbolic/kity-symbolic.svg" 2>/dev/null || true
            cp -f "${RES_DIR}/kity-listening-symbolic.svg" "${ICONS_DIR}/${theme}/apps/symbolic/kity-listening-symbolic.svg" 2>/dev/null || true
            cp -f "${RES_DIR}/kity-listening-symbolic.svg" "${ICONS_DIR}/${theme}/status/symbolic/kity-listening-symbolic.svg" 2>/dev/null || true
            cp -f "${RES_DIR}/kity.svg" "${APPS_DIR}/kity.svg" 2>/dev/null || true
            cp -f "${RES_DIR}/kity-listening.svg" "${APPS_DIR}/kity-listening.svg" 2>/dev/null || true
            cp -f "${RES_DIR}/org.cesar.kity.svg" "${APPS_DIR}/org.cesar.kity.svg" 2>/dev/null || true
        fi

        # Normalizar compatibilidad con temas oscuros para iconos simbólicos clave de Kity
        for sym_dir in "${ICONS_DIR}/${theme}/actions/symbolic" "${ICONS_DIR}/${theme}/actions@2x/symbolic" \
                       "${ICONS_DIR}/${theme}/devices/symbolic" "${ICONS_DIR}/${theme}/devices@2x/symbolic"; do
            if [ -d "${sym_dir}" ]; then
                # Asegurar ColorScheme-Text dinámico en document-edit, camera-photo y document-send
                sed -i 's/fill="#363636"/class="ColorScheme-Text" fill="currentColor"/g' "${sym_dir}/document-edit-symbolic.svg" 2>/dev/null || true
                sed -i 's/fill="#363636"/class="ColorScheme-Text" fill="currentColor"/g' "${sym_dir}/camera-photo-symbolic.svg" 2>/dev/null || true
                sed -i 's/fill="#363636"/class="ColorScheme-Text" fill="currentColor"/g' "${sym_dir}/document-send-symbolic.svg" 2>/dev/null || true
            fi
        done
    fi
    gtk-update-icon-cache -f -t "${ICONS_DIR}/${theme}" >/dev/null 2>&1 || true
done

echo "==> [5/5] Aplicando tema de iconos en KDE Plasma 6, Sycoca y GTK..."
kwriteconfig6 --file kdeglobals --group Icons --key Theme "WhiteSur"
mkdir -p "${HOME}/.config/kdedefaults"
kwriteconfig6 --file "${HOME}/.config/kdedefaults/kdeglobals" --group Icons --key Theme "WhiteSur"

# Forzar reconstrucción de la base de datos de servicios del sistema (KService/KSycoca)
if command -v kbuildsycoca6 >/dev/null 2>&1; then
    echo "    Reconstruyendo base de datos de tipos MIME y servicios KDE (kbuildsycoca6)..."
    kbuildsycoca6 --noincremental >/dev/null 2>&1 || true
fi

sed -i 's/^gtk-icon-theme-name=.*/gtk-icon-theme-name=WhiteSur/' "${HOME}/.config/gtk-3.0/settings.ini" 2>/dev/null || true
sed -i 's/^gtk-icon-theme-name=.*/gtk-icon-theme-name=WhiteSur/' "${HOME}/.config/gtk-4.0/settings.ini" 2>/dev/null || true

# Notificar al shell y reiniciar servicios de UI dependientes para recargar la caché en memoria
if systemctl --user is-active --quiet plasma-plasmashell.service; then
    echo "    Recargando plasmashell para refrescar paneles y escritorio..."
    systemctl --user restart plasma-plasmashell.service
fi

if systemctl --user is-active --quiet macos-dock.service; then
    echo "    Recargando macos-dock para refrescar dock y Launchpad..."
    systemctl --user restart macos-dock.service
fi

echo "==> Tema de iconos macOS Golden Gate (WhiteSur) configurado e integrado con éxito."
