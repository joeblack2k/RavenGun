#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Bouwt rg-macaw.exe (eigen Konami-lader) en libacio.dll (vervanger van Konami's libacio.dll) voor Silent Scope: Bone Eater
# met MinGW-w64 in de gepinde podman-container van de OpenParrot-build, zonder netwerk (zelfde opzet als tools/dev/build-pe-hook.sh).
#
# Gebruik: tools/rg-macaw/build.sh [--out DIR]   (standaard patches/SilentScopeBoneEater/dropin, of $RG_MACAW_OUT)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
CONTAINERFILE="$ROOT/packaging/containers/openparrot/Containerfile"
OUT="${RG_MACAW_OUT:-$ROOT/patches/SilentScopeBoneEater/dropin}"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUT="$2"; shift 2 ;;
    -h|--help) sed -n '2,7p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
done
IMAGE="localhost/ravengun-openparrot-build:$(sha256sum "$CONTAINERFILE" | cut -c1-12)"
podman image exists "$IMAGE" || { echo "image $IMAGE ontbreekt (bouw eerst met scripts/build-openparrot.sh)" >&2; exit 1; }
mkdir -p "$OUT"
CC=x86_64-w64-mingw32-gcc
podman run --rm --network=none -v "$HERE:/src:ro" -v "$OUT:/out" "$IMAGE" sh -c "
  $CC -shared -O2 -Wall -Wextra -Wno-cast-function-type -Wno-format-truncation -o /out/libacio.dll /src/rg_libacio.c -static-libgcc -Wl,--no-insert-timestamp &&
  $CC -O2 -Wall -Wextra -Wno-cast-function-type -Wno-format-truncation -o /out/rg-macaw.exe /src/rg_macaw.c -static-libgcc -lversion -Wl,--no-insert-timestamp"
( cd "$OUT" && sha256sum libacio.dll rg-macaw.exe )
