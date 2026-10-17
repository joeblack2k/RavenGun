#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Bouwt rg-pe-hook.dll (de generieke PE-inline-hook voor Windows-games, tools/rt-hasphook/rg_pe_hook*.c) met MinGW-w64 in de
# gepinde podman-container van de OpenParrot-build, zonder netwerk. Zelfde opzet als scripts/build-jvsbridge.sh.
#
# Gebruik: tools/dev/build-pe-hook.sh [--out DIR]     (standaard ~/.local/share/ravengun/rg-pe-hook/; $RAVENGUN_PE_HOOK_OUT)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/tools/rt-hasphook"
CONTAINERFILE="$ROOT/packaging/containers/openparrot/Containerfile"
OUT="${RAVENGUN_PE_HOOK_OUT:-${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/rg-pe-hook}"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUT="$2"; shift 2 ;;
    -h|--help) sed -n '2,7p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
done
IMAGE="localhost/ravengun-openparrot-build:$(sha256sum "$CONTAINERFILE" | cut -c1-12)"
if ! podman image exists "$IMAGE"; then
  echo "Image $IMAGE bouwen (eerste keer)..."
  podman build -q -t "$IMAGE" -f "$CONTAINERFILE" "$(dirname "$CONTAINERFILE")" >/dev/null
fi
mkdir -p "$OUT"
podman run --rm --network=none -v "$SRC:/src:ro" -v "$OUT:/out" "$IMAGE" \
  x86_64-w64-mingw32-gcc -shared -O2 -Wall -o /out/rg-pe-hook.dll /src/rg_pe_hook_dll.c /src/rg_pe_hook.c \
  -Wl,--image-base,0x180000000 -Wl,--no-insert-timestamp
{
  echo "rg-pe-hook.dll (GPL-3.0)"
  echo "bron:"; (cd "$ROOT" && sha256sum tools/rt-hasphook/rg_pe_hook_dll.c tools/rt-hasphook/rg_pe_hook.c tools/rt-hasphook/rg_pe_hook.h | sed 's/^/  /')
  echo "toolchain: $(podman run --rm --network=none "$IMAGE" x86_64-w64-mingw32-gcc --version | head -1)"
  echo "uitvoer:"; (cd "$OUT" && sha256sum rg-pe-hook.dll | sed 's/^/  /')
} > "$OUT/BUILDINFO.txt"
echo "klaar: $OUT/rg-pe-hook.dll"
