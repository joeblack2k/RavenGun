#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Bouwt rg-unis (tools/rg-unis/: de eigen UsbPluginsDll.dll van UNIS voor Elevator Action Invasion en Raccoon Rampage)
# met MinGW-w64 in de gepinde podman-container van de OpenParrot-build, zonder netwerk. Zelfde opzet als tools/dev/build-pe-hook.sh.
#
# Gebruik: tools/dev/build-unis.sh [--out MAP]...      standaard build/rg-unis/; MAP is meestal patches/<Game>/dropin
# De DLL heet in de uitvoer UsbPluginsDll.dll (de naam die de game laadt) en komt met BUILDINFO-unis.txt.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/tools/rg-unis"
CONTAINERFILE="$ROOT/packaging/containers/openparrot/Containerfile"
OUTS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUTS+=("$2"); shift 2 ;;
    -h|--help) sed -n '3,7p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
done
[[ ${#OUTS[@]} -gt 0 ]] || OUTS=("$ROOT/build/rg-unis")
IMAGE="localhost/ravengun-openparrot-build:$(sha256sum "$CONTAINERFILE" | cut -c1-12)"
if ! podman image exists "$IMAGE"; then
  echo "Image $IMAGE bouwen (eerste keer)..."
  podman build -q -t "$IMAGE" -f "$CONTAINERFILE" "$(dirname "$CONTAINERFILE")" >/dev/null
fi
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
podman run --rm --network=none -v "$SRC:/src:ro" -v "$TMP:/out" "$IMAGE" \
  x86_64-w64-mingw32-gcc -shared -O2 -s -std=c11 -Wall -Wextra -Werror -static-libgcc \
  -o /out/UsbPluginsDll.dll /src/rg_unis.c /src/rg_unis_kern.c \
  -Wl,--no-insert-timestamp
{
  echo "UsbPluginsDll.dll = rg-unis (GPL-3.0), RavenGuns eigen UNIS-bord"
  echo "bron:"; (cd "$ROOT" && sha256sum tools/rg-unis/rg_unis.c tools/rg-unis/rg_unis_kern.c tools/rg-unis/rg_unis_kern.h | sed 's/^/  /')
  echo "toolchain: $(podman run --rm --network=none "$IMAGE" x86_64-w64-mingw32-gcc --version | head -1)"
  echo "uitvoer:"; (cd "$TMP" && sha256sum UsbPluginsDll.dll | sed 's/^/  /')
} > "$TMP/BUILDINFO-unis.txt"
for o in "${OUTS[@]}"; do
  mkdir -p "$o"
  cp "$TMP/UsbPluginsDll.dll" "$TMP/BUILDINFO-unis.txt" "$o/"
  echo "klaar: $o/UsbPluginsDll.dll"
done
