#!/usr/bin/env bash
# Build the Linux download: one file that runs on any distribution, because
# the Qt libraries it needs travel inside it rather than being installed.
#
# Needs no root. The three tools it uses are themselves AppImages and are
# fetched into build/ on first run.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD=${BUILD:-$ROOT/build/appimage}
TOOLS=${TOOLS:-$ROOT/build/appimage-tools}
APPDIR=$BUILD/AppDir
mkdir -p "$TOOLS"

fetch() {  # fetch <name> <url>
    if [ ! -x "$TOOLS/$1" ]; then
        echo "--- fetching $1"
        curl -sSLo "$TOOLS/$1" "$2"
        chmod +x "$TOOLS/$1"
    fi
}
base=https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous
fetch linuxdeploy         "$base/linuxdeploy-x86_64.AppImage"
fetch linuxdeploy-plugin-qt \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage

echo "--- building"
# CMakeLists already says which way it went, and find_package(Qt6) is QUIET, so
# that message is the only signal there is. Without checking it the failure
# arrives later as "unknown target 'rv32-gui'", which names the symptom only.
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release 2>&1 \
    | tee "$BUILD.configure.log"
if grep -q "Qt6 not found" "$BUILD.configure.log"; then
    echo
    echo "error: Qt6 was not usable, so there is no rv32-gui to package." >&2
    echo "       the configure log above says what was missing; on a Debian" >&2
    echo "       or Ubuntu that is usually qt6-base-dev and libgl1-mesa-dev." >&2
    exit 1
fi
cmake --build "$BUILD" --target rv32-gui rv32

echo "--- staging AppDir"
rm -rf "$APPDIR"
install -Dm755 "$BUILD/rv32-gui"           "$APPDIR/usr/bin/rv32-gui"
install -Dm755 "$BUILD/rv32"               "$APPDIR/usr/bin/rv32"
install -Dm644 "$ROOT/packaging/rv32.desktop" \
               "$APPDIR/usr/share/applications/rv32.desktop"
for size in 16 24 32 48 64 128 256; do
    install -Dm644 "$ROOT/packaging/icons/rv32-$size.png" \
        "$APPDIR/usr/share/icons/hicolor/${size}x${size}/apps/rv32.png"
done
install -Dm644 "$ROOT/packaging/rv32.svg" \
               "$APPDIR/usr/share/icons/hicolor/scalable/apps/rv32.svg"
# Something to open, alongside the program that opens it.
mkdir -p "$APPDIR/usr/share/rv32"
cp -r "$ROOT/examples" "$ROOT/docs" "$APPDIR/usr/share/rv32/"

echo "--- bundling Qt"
export QMAKE=${QMAKE:-$(command -v qmake6 || command -v qmake)}
export EXTRA_QT_PLUGINS=${EXTRA_QT_PLUGINS:-styles;imageformats}
# The Qt plugin bundles the platform it sees in use -- xcb -- and no other, so
# an AppImage built on a desktop cannot start headlessly. That is the same
# omission the Windows bundle had with qoffscreen.dll, and it has to be asked
# for by name here too. xcb stays the one a desktop actually uses; on Wayland
# it goes through XWayland.
export EXTRA_PLATFORM_PLUGINS=${EXTRA_PLATFORM_PLUGINS:-libqoffscreen.so;libqminimal.so}
# linuxdeploy insists on a writable, isolated output directory.
cd "$BUILD"
"$TOOLS/linuxdeploy" --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/rv32-gui" \
    --executable "$APPDIR/usr/bin/rv32" \
    --desktop-file "$APPDIR/usr/share/applications/rv32.desktop" \
    --icon-file "$ROOT/packaging/icons/rv32-256.png" \
    --plugin qt \
    --output appimage

out=$(ls -t "$BUILD"/*.AppImage 2>/dev/null | head -1)
[ -n "$out" ] || { echo "no AppImage produced"; exit 1; }
final=$ROOT/build/rv32-x86_64.AppImage
mv "$out" "$final"
chmod +x "$final"
echo
echo "built: $final  ($(du -h "$final" | cut -f1))"
