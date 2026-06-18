#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 3 ]]; then
  echo "Usage: $0 <build-dir> <qt-root> <version>" >&2
  exit 2
fi

BUILD_DIR=$(realpath "$1")
QT_ROOT=$(realpath "$2")
VERSION="$3"

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
OUT_DIR="$ROOT/dist"
PKG_ROOT="$ROOT/build/linux-deb/pkg"
APP_ROOT="$PKG_ROOT/opt/tnuxmusic"
BIN_DIR="$APP_ROOT/bin"
LIB_DIR="$APP_ROOT/lib"
PLUGIN_DIR="$APP_ROOT/plugins"
QML_DIR="$APP_ROOT/qml"

APP_BIN="$BUILD_DIR/tnuxmusic"
if [[ ! -x "$APP_BIN" ]]; then
  echo "Missing executable: $APP_BIN" >&2
  exit 1
fi

if [[ ! -d "$QT_ROOT/lib" || ! -d "$QT_ROOT/plugins" || ! -d "$QT_ROOT/qml" ]]; then
  echo "Invalid Qt root: $QT_ROOT" >&2
  exit 1
fi

rm -rf "$PKG_ROOT"
mkdir -p "$BIN_DIR" "$LIB_DIR" "$PLUGIN_DIR" "$QML_DIR" "$OUT_DIR"

install -m 0755 "$APP_BIN" "$BIN_DIR/tnuxmusic.real"

cat > "$BIN_DIR/tnuxmusic" <<'EOF'
#!/usr/bin/env bash
APPDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export LD_LIBRARY_PATH="$APPDIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$APPDIR/plugins"
export QML2_IMPORT_PATH="$APPDIR/qml"
export QT_QUICK_CONTROLS_STYLE=Material
exec "$APPDIR/bin/tnuxmusic.real" "$@"
EOF
chmod 0755 "$BIN_DIR/tnuxmusic"

mkdir -p "$PKG_ROOT/usr/bin"
ln -s /opt/tnuxmusic/bin/tnuxmusic "$PKG_ROOT/usr/bin/tnuxmusic"

copy_dir() {
  local src="$1"
  local dst="$2"
  [[ -e "$src" ]] || return 0
  mkdir -p "$(dirname "$dst")"
  cp -a "$src" "$dst"
}

copy_dir "$QT_ROOT/plugins/platforms" "$PLUGIN_DIR/platforms"
copy_dir "$QT_ROOT/plugins/xcbglintegrations" "$PLUGIN_DIR/xcbglintegrations"
copy_dir "$QT_ROOT/plugins/multimedia" "$PLUGIN_DIR/multimedia"
copy_dir "$QT_ROOT/plugins/imageformats" "$PLUGIN_DIR/imageformats"
copy_dir "$QT_ROOT/plugins/iconengines" "$PLUGIN_DIR/iconengines"
copy_dir "$QT_ROOT/plugins/platforminputcontexts" "$PLUGIN_DIR/platforminputcontexts"
copy_dir "$QT_ROOT/plugins/platformthemes" "$PLUGIN_DIR/platformthemes"
copy_dir "$QT_ROOT/plugins/tls" "$PLUGIN_DIR/tls"
copy_dir "$QT_ROOT/plugins/networkinformation" "$PLUGIN_DIR/networkinformation"
copy_dir "$QT_ROOT/plugins/wayland-decoration-client" "$PLUGIN_DIR/wayland-decoration-client"
copy_dir "$QT_ROOT/plugins/wayland-graphics-integration-client" "$PLUGIN_DIR/wayland-graphics-integration-client"
copy_dir "$QT_ROOT/plugins/wayland-shell-integration" "$PLUGIN_DIR/wayland-shell-integration"

copy_dir "$QT_ROOT/qml/QtCore" "$QML_DIR/QtCore"
copy_dir "$QT_ROOT/qml/QtQml" "$QML_DIR/QtQml"
copy_dir "$QT_ROOT/qml/QtQuick" "$QML_DIR/QtQuick"
copy_dir "$QT_ROOT/qml/QtMultimedia" "$QML_DIR/QtMultimedia"
copy_dir "$QT_ROOT/qml/QtNetwork" "$QML_DIR/QtNetwork"
[[ -f "$QT_ROOT/qml/builtins.qmltypes" ]] && install -m 0644 "$QT_ROOT/qml/builtins.qmltypes" "$QML_DIR/builtins.qmltypes"

copy_qt_deps() {
  local file="$1"
  [[ -f "$file" ]] || return 0
  file "$file" | grep -q 'ELF' || return 0
  (LD_LIBRARY_PATH="$LIB_DIR:$QT_ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ldd "$file" 2>/dev/null || true) \
    | awk '{ for (i = 1; i <= NF; ++i) if ($i ~ /^\//) print $i }' \
    | while read -r dep; do
        case "$dep" in
          "$QT_ROOT"/lib/*)
            local base
            base=$(basename "$dep")
            if [[ ! -e "$LIB_DIR/$base" ]]; then
              cp -L "$dep" "$LIB_DIR/$base"
              chmod 0644 "$LIB_DIR/$base"
            fi
            ;;
        esac
      done
}

previous_count=-1
while true; do
  while IFS= read -r elf; do
    copy_qt_deps "$elf"
  done < <(find "$BIN_DIR" "$PLUGIN_DIR" "$QML_DIR" "$LIB_DIR" -type f)

  current_count=$(find "$LIB_DIR" -type f | wc -l)
  [[ "$current_count" == "$previous_count" ]] && break
  previous_count="$current_count"
done

find "$BIN_DIR" "$LIB_DIR" "$PLUGIN_DIR" "$QML_DIR" -type f -perm -111 -exec strip --strip-unneeded {} + 2>/dev/null || true

mkdir -p "$PKG_ROOT/usr/share/applications"
install -m 0644 "$ROOT/assets/linux/tnuxmusic.desktop.in" "$PKG_ROOT/usr/share/applications/tnuxmusic.desktop"

for size in 16 24 32 48 64 128 256 512; do
  icon_dir="$PKG_ROOT/usr/share/icons/hicolor/${size}x${size}/apps"
  mkdir -p "$icon_dir"
  install -m 0644 "$ROOT/assets/icons/tnuxmusic-${size}.png" "$icon_dir/tnuxmusic.png"
done
mkdir -p "$PKG_ROOT/usr/share/icons/hicolor/scalable/apps"
install -m 0644 "$ROOT/assets/icons/tnuxmusic.svg" "$PKG_ROOT/usr/share/icons/hicolor/scalable/apps/tnuxmusic.svg"

mkdir -p "$PKG_ROOT/DEBIAN"
installed_size=$(du -sk "$PKG_ROOT" | awk '{print $1}')
cat > "$PKG_ROOT/DEBIAN/control" <<EOF
Package: tnuxmusic
Version: ${VERSION#v}
Section: sound
Priority: optional
Architecture: amd64
Maintainer: tnuxmusic <tnuxmusic@example.invalid>
Installed-Size: $installed_size
Depends: libc6, libstdc++6, libgcc-s1, libssl3, libgl1, libegl1, libx11-6, libxcb1, libxkbcommon0, libpulse0, libfontconfig1, libfreetype6, libglib2.0-0, libdbus-1-3, libxcb-cursor0, libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-randr0, libxcb-render-util0, libxcb-shape0, libxcb-xfixes0, libxcb-xinerama0, libxcb-xkb1, libxrender1, libxi6, libxext6, libxrandr2
Description: Qt 6 desktop music player with NCM import support
 tnuxmusic is a local music library manager and player with NCM import,
 localized library export, album wall, playback queue, playlists and TLY lyrics.
EOF

cat > "$PKG_ROOT/DEBIAN/postinst" <<'EOF'
#!/usr/bin/env bash
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database /usr/share/applications || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  gtk-update-icon-cache -q /usr/share/icons/hicolor || true
fi
EOF
chmod 0755 "$PKG_ROOT/DEBIAN/postinst"

cat > "$PKG_ROOT/DEBIAN/postrm" <<'EOF'
#!/usr/bin/env bash
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database /usr/share/applications || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  gtk-update-icon-cache -q /usr/share/icons/hicolor || true
fi
EOF
chmod 0755 "$PKG_ROOT/DEBIAN/postrm"

OUT_DEB="$OUT_DIR/tnuxmusic-${VERSION}-linux-amd64.deb"
dpkg-deb --build --root-owner-group "$PKG_ROOT" "$OUT_DEB"
echo "$OUT_DEB"
