#!/usr/bin/env bash
# Put rv32 in the applications menu, so it starts from a double-click.
#
# Everything lands under $HOME: no root, and nothing outside these four paths
# is touched. Run with --uninstall to take it all back out.
#
#     ./install.sh [path/to/rv32-x86_64.AppImage]
#     ./install.sh --uninstall
set -euo pipefail

PREFIX=${XDG_DATA_HOME:-$HOME/.local/share}
LIBDIR=$HOME/.local/lib/rv32
BINDIR=$HOME/.local/bin
DESKTOP=$PREFIX/applications/rv32.desktop
ICONS=$PREFIX/icons/hicolor

refresh() {
    command -v update-desktop-database > /dev/null && \
        update-desktop-database "$PREFIX/applications" 2>/dev/null || true
    command -v gtk-update-icon-cache > /dev/null && \
        gtk-update-icon-cache -qtf "$ICONS" 2>/dev/null || true
}

if [ "${1:-}" = "--uninstall" ]; then
    rm -rf "$LIBDIR"
    rm -f "$DESKTOP" "$BINDIR/rv32" "$BINDIR/rv32-gui"
    find "$ICONS" -name 'rv32.png' -o -name 'rv32.svg' 2>/dev/null | xargs -r rm -f
    refresh
    echo "removed."
    exit 0
fi

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
APPIMAGE=${1:-}
if [ -z "$APPIMAGE" ]; then
    APPIMAGE=$(ls -t "$here"/rv32-x86_64.AppImage "$here"/../build/rv32-x86_64.AppImage \
                     ./rv32-x86_64.AppImage 2>/dev/null | head -1 || true)
fi
[ -n "$APPIMAGE" ] && [ -f "$APPIMAGE" ] || {
    echo "usage: $0 [path/to/rv32-x86_64.AppImage]" >&2; exit 1; }
APPIMAGE=$(cd "$(dirname "$APPIMAGE")" && pwd)/$(basename "$APPIMAGE")

# Unpacked rather than left as a single file. An AppImage needs libfuse2 to
# mount itself, which several current distributions no longer install by
# default -- and a double-click that silently does nothing is the worst way to
# find that out. Unpacking costs disk and removes the dependency entirely.
echo "unpacking..."
chmod +x "$APPIMAGE"
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
( cd "$work" && "$APPIMAGE" --appimage-extract > /dev/null )
rm -rf "$LIBDIR"; mkdir -p "$(dirname "$LIBDIR")"
mv "$work/squashfs-root" "$LIBDIR"

echo "installing..."
mkdir -p "$BINDIR" "$PREFIX/applications"
ln -sfn "$LIBDIR/usr/bin/rv32"     "$BINDIR/rv32"
ln -sfn "$LIBDIR/AppRun"           "$BINDIR/rv32-gui"

for png in "$LIBDIR"/usr/share/icons/hicolor/*/apps/rv32.png; do
    [ -f "$png" ] || continue
    size=$(basename "$(dirname "$(dirname "$png")")")
    install -Dm644 "$png" "$ICONS/$size/apps/rv32.png"
done
[ -f "$LIBDIR/usr/share/icons/hicolor/scalable/apps/rv32.svg" ] && \
    install -Dm644 "$LIBDIR/usr/share/icons/hicolor/scalable/apps/rv32.svg" \
                   "$ICONS/scalable/apps/rv32.svg"

# Written here rather than copied, because Exec has to be an absolute path and
# only now is it known.
sed -e "s|^Exec=.*|Exec=$LIBDIR/AppRun %f|" \
    "$LIBDIR/usr/share/applications/rv32.desktop" > "$DESKTOP"
chmod +x "$DESKTOP"

refresh
# So a .s file opens with a double-click too, not only the program itself.
command -v xdg-mime > /dev/null && xdg-mime default rv32.desktop text/x-asm 2>/dev/null || true

echo
echo "rv32 is in the applications menu -- search for it, or double-click a .s file."
echo "  program   $LIBDIR"
echo "  menu      $DESKTOP"
echo "  terminal  rv32, rv32-gui   (from $BINDIR)"
case ":$PATH:" in
    *":$BINDIR:"*) ;;
    *) echo; echo "note: $BINDIR is not on PATH, so the two terminal commands"
       echo "      will not resolve until it is." ;;
esac
echo
echo "examples are in $LIBDIR/usr/share/rv32/examples"
echo "uninstall with: $0 --uninstall"
