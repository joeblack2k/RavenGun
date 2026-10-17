#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Bouwt bnusio.dll (RavenGun): de USIO-bibliotheek voor Point Blank X (tools/rg-usio/rg_usio.cpp + usio_model.h, 32-bit). MinGW-w64 (i686) in
# de gepinde podman-container van de OpenParrot-build, zonder netwerk. Het resultaat is een drop-in (docs/dropins.md) en vervangt het
# echte Plugins/bnusio.dll in de view van de game.
#
# Gebruik: tools/dev/build-usio.sh [--out DIR]...
#   --out DIR   waar bnusio.dll heen gaat (herhaalbaar); standaard patches/PointBlankX/dropin.kandidaat.d
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
[[ ${#OUTS[@]} -gt 0 ]] || OUTS=("$ROOT/patches/PointBlankX/dropin.kandidaat.d")

IMAGE="localhost/ravengun-openparrot-build:$(sha256sum "$CONTAINERFILE" | cut -c1-12)"
if ! podman image exists "$IMAGE"; then
  echo "Image $IMAGE bouwen (eerste keer)..."
  podman build -q -t "$IMAGE" -f "$CONTAINERFILE" "$(dirname "$CONTAINERFILE")" >/dev/null
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/src/tools/rg-usio" "$TMP/out"
cp "$ROOT/tools/rg-usio/rg_usio.cpp" "$ROOT/tools/rg-usio/usio_model.h" "$TMP/src/tools/rg-usio/"
# Statisch gelinkt (geen libstdc++/libgcc-DLL's in het spelproces); geen tijdstempel voor reproduceerbaarheid. De namen worden zonder
# decoratie geëxporteerd (cdecl, zoals het echte bestand).
podman run --rm --network=none -v "$TMP/src:/src:ro" -v "$TMP/out:/out" "$IMAGE" \
  i686-w64-mingw32-g++ -std=c++17 -O2 -s -shared -Wall -Wextra -Werror \
  -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp \
  /src/tools/rg-usio/rg_usio.cpp -o /out/bnusio.dll

for o in "${OUTS[@]}"; do
  mkdir -p "$o"
  cp "$TMP/out/bnusio.dll" "$o/bnusio.dll"
  echo "klaar: $o/bnusio.dll"
done
echo "bron: $(sha256sum "$ROOT/tools/rg-usio/rg_usio.cpp" | cut -c1-16) rg_usio.cpp; dll: $(sha256sum "$TMP/out/bnusio.dll" | cut -c1-16)"
