#!/usr/bin/env bash
# ==============================================================================
# setup-fonts.sh - Aprovisionamiento e integración tipográfica macOS Golden Gate
# ==============================================================================
# Configura las tipografías oficiales de Apple (SF Pro, SF Mono, New York) en
# CachyOS / KDE Plasma 6 bajo Wayland estricto, respetando las directrices HIG.
# ==============================================================================

set -euo pipefail

FONTS_DIR="${HOME}/.local/share/fonts/apple-fonts"
FONTCONFIG_DIR="${HOME}/.config/fontconfig"
CHAOTIC_PKG_URL="https://cdn-mirror.chaotic.cx/chaotic-aur/x86_64/apple-fonts-7.0.6-1-any.pkg.tar.zst"

echo "==> [1/4] Verificando e instalando familias tipográficas de Apple..."
mkdir -p "${FONTS_DIR}"

if ! fc-list : family | grep -q "^SF Pro$"; then
    echo "    Descargando paquete oficial de fuentes de Apple desde repositorio optimizado..."
    curl -L --fail --progress-bar "${CHAOTIC_PKG_URL}" | bsdtar -xf - -C "${FONTS_DIR}" --strip-components=4 usr/share/fonts/apple-fonts
    echo "    Fuentes extraídas exitosamente en: ${FONTS_DIR}"
else
    echo "    SF Pro ya se encuentra disponible en el sistema."
fi

echo "==> [2/4] Regenerando caché de fuentes (fontconfig)..."
fc-cache -fv "${FONTS_DIR}" >/dev/null

echo "==> [3/4] Configurando Fontconfig para aliases del sistema y motor Quartz..."
mkdir -p "${FONTCONFIG_DIR}"
cat << 'EOF' > "${FONTCONFIG_DIR}/fonts.conf"
<?xml version='1.0'?>
<!DOCTYPE fontconfig SYSTEM 'urn:fontconfig:fonts.dtd'>
<fontconfig>
  <dir>~/.local/share/fonts</dir>

  <!-- Preferencias de familias del sistema macOS Golden Gate -->
  <match target="pattern">
    <test name="family" qual="any">
      <string>sans-serif</string>
    </test>
    <edit name="family" mode="prepend" binding="strong">
      <string>SF Pro</string>
      <string>Noto Sans</string>
      <string>Noto Color Emoji</string>
    </edit>
  </match>

  <match target="pattern">
    <test name="family" qual="any">
      <string>serif</string>
    </test>
    <edit name="family" mode="prepend" binding="strong">
      <string>New York</string>
      <string>Noto Serif</string>
    </edit>
  </match>

  <match target="pattern">
    <test name="family" qual="any">
      <string>monospace</string>
    </test>
    <edit name="family" mode="prepend" binding="strong">
      <string>SF Mono</string>
      <string>Noto Sans Mono</string>
    </edit>
  </match>

  <!-- Aliases web y del ecosistema Apple -->
  <match target="pattern">
    <test name="family" qual="any">
      <string>system-ui</string>
    </test>
    <edit name="family" mode="prepend" binding="strong">
      <string>SF Pro</string>
    </edit>
  </match>

  <match target="pattern">
    <test name="family" qual="any">
      <string>-apple-system</string>
    </test>
    <edit name="family" mode="prepend" binding="strong">
      <string>SF Pro</string>
    </edit>
  </match>

  <match target="pattern">
    <test name="family" qual="any">
      <string>BlinkMacSystemFont</string>
    </test>
    <edit name="family" mode="prepend" binding="strong">
      <string>SF Pro</string>
    </edit>
  </match>

  <!-- Renderizado y suavizado idéntico al motor Quartz / Core Text de macOS -->
  <match target="font">
    <edit mode="assign" name="antialias">
      <bool>true</bool>
    </edit>
    <edit mode="assign" name="hinting">
      <bool>true</bool>
    </edit>
    <edit mode="assign" name="hintstyle">
      <const>hintslight</const>
    </edit>
    <edit mode="assign" name="rgba">
      <const>rgb</const>
    </edit>
    <edit mode="assign" name="lcdfilter">
      <const>lcddefault</const>
    </edit>
  </match>

  <!-- Oblicuas sintéticas -->
  <match target="font">
    <test name="slant">
      <const>roman</const>
    </test>
    <test compare="not_eq" name="slant" target="pattern">
      <const>roman</const>
    </test>
    <edit mode="assign" name="matrix">
      <times>
        <name>matrix</name>
        <matrix>
          <double>1</double>
          <double>0.2</double>
          <double>0</double>
          <double>1</double>
        </matrix>
      </times>
    </edit>
    <edit mode="assign" name="slant">
      <const>oblique</const>
    </edit>
    <edit mode="assign" name="embeddedbitmap">
      <bool>false</bool>
    </edit>
  </match>
</fontconfig>
EOF
fc-cache -f "${FONTCONFIG_DIR}" >/dev/null

echo "==> [4/4] Aplicando configuración tipográfica en KDE Plasma 6 y GTK..."
# Métricas exactas macOS HIG (96 DPI equivalente a 13pt base macOS):
kwriteconfig6 --file kdeglobals --group General --key font "SF Pro,10,-1,5,400,0,0,0,0,0,0,0,0,0,0,1"
kwriteconfig6 --file kdeglobals --group General --key fixed "SF Mono,10,-1,5,400,0,0,0,0,0,0,0,0,0,0,1"
kwriteconfig6 --file kdeglobals --group General --key smallestReadableFont "SF Pro,8,-1,5,400,0,0,0,0,0,0,0,0,0,0,1"
kwriteconfig6 --file kdeglobals --group General --key toolBarFont "SF Pro,10,-1,5,400,0,0,0,0,0,0,0,0,0,0,1"
kwriteconfig6 --file kdeglobals --group General --key menuFont "SF Pro,10,-1,5,400,0,0,0,0,0,0,0,0,0,0,1"
kwriteconfig6 --file kdeglobals --group WM --key activeFont "SF Pro,10,-1,5,600,0,0,0,0,0,0,0,0,0,0,1"

# GTK 3, 4, Konsole y Yakuake
sed -i 's/^gtk-font-name=.*/gtk-font-name=SF Pro 10/' "${HOME}/.config/gtk-3.0/settings.ini" 2>/dev/null || true
sed -i 's/^gtk-font-name=.*/gtk-font-name=SF Pro 10/' "${HOME}/.config/gtk-4.0/settings.ini" 2>/dev/null || true
echo 'gtk-font-name="SF Pro 10"' > "${HOME}/.gtkrc-2.0"

mkdir -p "${HOME}/.local/share/konsole"
KONSOLE_PROFILE="${HOME}/.local/share/konsole/cachyos.profile"
if [ ! -f "${KONSOLE_PROFILE}" ]; then
    kwriteconfig6 --file "${KONSOLE_PROFILE}" --group General --key Name "cachyos"
fi
kwriteconfig6 --file "${KONSOLE_PROFILE}" --group Appearance --key Font "SF Mono,10,-1,5,400,0,0,0,0,0,0,0,0,0,0,1"
kwriteconfig6 --file "${HOME}/.config/konsolerc" --group "Desktop Entry" --key DefaultProfile "cachyos.profile"

# Plasmoides: Actualizar reloj digital en appletsrc si existiera override
APPLETS_RC="${HOME}/.config/plasma-org.kde.plasma.desktop-appletsrc"
if [ -f "${APPLETS_RC}" ]; then
    sed -i 's/^fontFamily=Noto Sans/fontFamily=SF Pro/' "${APPLETS_RC}" 2>/dev/null || true
fi

# Recargar Yakuake si está en ejecución para aplicar SF Mono en memoria
if pgrep -x yakuake >/dev/null 2>&1; then
    echo "    Recargando instancia activa de Yakuake..."
    qdbus6 org.kde.yakuake /MainApplication org.qtproject.Qt.QCoreApplication.quit 2>/dev/null || killall yakuake 2>/dev/null || true
    sleep 0.5
    kstart yakuake >/dev/null 2>&1 &
fi

# Recargar plasmashell para inyectar SF Pro en todos los applets y elementos gráficos del escritorio
if systemctl --user is-active --quiet plasma-plasmashell.service; then
    echo "    Recargando plasmashell para refrescar applets y paneles..."
    systemctl --user restart plasma-plasmashell.service
fi

echo "==> Tipografía macOS Golden Gate (SF Pro, SF Mono, New York) configurada correctamente."
