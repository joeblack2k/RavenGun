#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Bouwt rg-drakons (tools/rg-drakons/: de eigen RS_232_DLL.dll van Drakon's Realm Keepers, met de muisinjectie uit het bordbeeld)
# met MinGW-w64 in de gepinde podman-container van de OpenParrot-build, zonder netwerk. Zelfde opzet als tools/dev/build-adrio.sh.
#
# Gebruik: tools/dev/build-drakons.sh [--out MAP]...      standaard build/rg-drakons/; MAP is meestal patches/<Game>/dropin
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/tools/rg-drakons"
CONTAINERFILE="$ROOT/packaging/containers/openparrot/Containerfile"
OUTS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUTS+=("$2"); shift 2 ;;
    -h|--help) sed -n '3,6p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
done
[[ ${#OUTS[@]} -gt 0 ]] || OUTS=("$ROOT/build/rg-drakons")
IMAGE="localhost/ravengun-openparrot-build:$(sha256sum "$CONTAINERFILE" | cut -c1-12)"
if ! podman image exists "$IMAGE"; then
  echo "Image $IMAGE bouwen (eerste keer)..."
  podman build -q -t "$IMAGE" -f "$CONTAINERFILE" "$(dirname "$CONTAINERFILE")" >/dev/null
fi
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
podman run --rm --network=none -v "$SRC:/src:ro" -v "$TMP:/out" "$IMAGE" \
  x86_64-w64-mingw32-gcc -shared -O2 -s -std=c11 -Wall -Wextra -Werror -static-libgcc \
  -o /out/RS_232_DLL.dll /src/rg_rs232.c -Wl,--no-insert-timestamp
{
  echo "RS_232_DLL.dll = rg-drakons (GPL-3.0), RavenGuns eigen seriele laag + muisinjectie voor Drakon's Realm Keepers"
  echo "bron:"; (cd "$ROOT" && sha256sum tools/rg-drakons/rg_rs232.c | sed 's/^/  /')
  echo "toolchain: $(podman run --rm --network=none "$IMAGE" x86_64-w64-mingw32-gcc --version | head -1)"
  echo "uitvoer:"; (cd "$TMP" && sha256sum RS_232_DLL.dll | sed 's/^/  /')
} > "$TMP/BUILDINFO-drakons.txt"
for o in "${OUTS[@]}"; do
  mkdir -p "$o"
  cp "$TMP/RS_232_DLL.dll" "$TMP/BUILDINFO-drakons.txt" "$o/"
  echo "klaar: $o/RS_232_DLL.dll"
done
