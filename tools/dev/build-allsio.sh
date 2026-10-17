#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Bouwt rg-allsio.dll (RavenGun, 64-bit): het Sega ALLS USB-IO-bord voor amdaemon.exe (tools/rg-allsio/rg_allsio.cpp + allsio_proto.h).
# MinGW-w64 (x86_64) in de gepinde podman-container van de OpenParrot-build, zonder netwerk. Het resultaat is een preload-drop-in
# (docs/dropins.md, Preload = true): de lader laadt hem via TP_PRELOAD_DLL in elk proces van het spel; hij doet alleen iets in amdaemon.exe.
#
# Gebruik: tools/dev/build-allsio.sh [--out DIR]...
#   --out DIR   waar rg-allsio.dll heen gaat (herhaalbaar); standaard docs/research/namco/patches/HOTDSD/dropin
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CONTAINERFILE="$ROOT/packaging/containers/openparrot/Containerfile"
OUTS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUTS+=("$2"); shift 2 ;;
    -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
done
[[ ${#OUTS[@]} -gt 0 ]] || OUTS=("$ROOT/docs/research/namco/patches/HOTDSD/dropin")

IMAGE="localhost/ravengun-openparrot-build:$(sha256sum "$CONTAINERFILE" | cut -c1-12)"
if ! podman image exists "$IMAGE"; then
  echo "Image $IMAGE bouwen (eerste keer)..."
  podman build -q -t "$IMAGE" -f "$CONTAINERFILE" "$(dirname "$CONTAINERFILE")" >/dev/null
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/src/tools/rg-allsio" "$TMP/out"
cp "$ROOT/tools/rg-allsio/rg_allsio.cpp" "$ROOT/tools/rg-allsio/allsio_proto.h" "$TMP/src/tools/rg-allsio/"
# Statisch gelinkt (geen libstdc++/libgcc-DLL's in het spelproces); geen tijdstempel voor reproduceerbaarheid.
podman run --rm --network=none -v "$TMP/src:/src:ro" -v "$TMP/out:/out" "$IMAGE" \
  x86_64-w64-mingw32-g++ -std=c++17 -O2 -s -shared -Wall -Wextra -Werror \
  -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp \
  /src/tools/rg-allsio/rg_allsio.cpp -o /out/rg-allsio.dll

for o in "${OUTS[@]}"; do
  mkdir -p "$o"
  cp "$TMP/out/rg-allsio.dll" "$o/rg-allsio.dll"
  echo "klaar: $o/rg-allsio.dll"
done
echo "bron: $(sha256sum "$ROOT/tools/rg-allsio/rg_allsio.cpp" | cut -c1-16) rg_allsio.cpp; dll: $(sha256sum "$TMP/out/rg-allsio.dll" | cut -c1-16)"
