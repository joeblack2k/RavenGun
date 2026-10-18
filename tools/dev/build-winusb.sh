#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Bouwt winusb.dll (RavenGun, 64-bit): het USIO-bord van Time Crisis 5 (tools/rg-winusb/rg_winusb.cpp + usio_board.h). MinGW-w64 (x86_64)
# in de gepinde podman-container van de OpenParrot-build, zonder netwerk. Het resultaat is een drop-in (docs/dropins.md) naast
# TimeCrisisGame-Win64-Shipping.exe en wordt met WINEDLLOVERRIDES (winusb=n) afgedwongen.
#
# Gebruik: tools/dev/build-winusb.sh [--out DIR]...
#   --out DIR   waar winusb.dll heen gaat (herhaalbaar); standaard docs/research/namco/patches/TC5/dropin
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
[[ ${#OUTS[@]} -gt 0 ]] || OUTS=("$ROOT/docs/research/namco/patches/TC5/dropin")

IMAGE="localhost/ravengun-openparrot-build:$(sha256sum "$CONTAINERFILE" | cut -c1-12)"
if ! podman image exists "$IMAGE"; then
  echo "Image $IMAGE bouwen (eerste keer)..."
  podman build -q -t "$IMAGE" -f "$CONTAINERFILE" "$(dirname "$CONTAINERFILE")" >/dev/null
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/src/tools/rg-winusb" "$TMP/out"
cp "$ROOT/tools/rg-winusb/rg_winusb.cpp" "$ROOT/tools/rg-winusb/usio_board.h" "$ROOT/tools/rg-winusb/gun_com.h" "$TMP/src/tools/rg-winusb/"
# Statisch gelinkt (geen libstdc++/libgcc-DLL's in het spelproces); geen tijdstempel voor reproduceerbaarheid. user32 voor SendInput.
podman run --rm --network=none -v "$TMP/src:/src:ro" -v "$TMP/out:/out" "$IMAGE" \
  x86_64-w64-mingw32-g++ -std=c++17 -O2 -s -shared -Wall -Wextra -Werror \
  -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp \
  /src/tools/rg-winusb/rg_winusb.cpp -o /out/winusb.dll -luser32

for o in "${OUTS[@]}"; do
  mkdir -p "$o"
  cp "$TMP/out/winusb.dll" "$o/winusb.dll"
  echo "klaar: $o/winusb.dll"
done
echo "bron: $(sha256sum "$ROOT/tools/rg-winusb/rg_winusb.cpp" | cut -c1-16) rg_winusb.cpp; dll: $(sha256sum "$TMP/out/winusb.dll" | cut -c1-16)"
