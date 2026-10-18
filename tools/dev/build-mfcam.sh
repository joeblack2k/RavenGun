#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Bouwt rg-mfcam.dll: de Media Foundation-camera van RavenGun voor een 32-bits Windows-spel onder Wine (tools/rg-mfcam/rg_mfcam.cpp;
# het camerabeeld komt van de daemon, src/ravengun/vision/camera_feed.hpp). MinGW-w64 (i686) in de gepinde podman-container van de
# OpenParrot-build, zonder netwerk. Het resultaat is een drop-in (docs/dropins.md) en hoort in de map van elke game die hem gebruikt.
#
# Gebruik: tools/dev/build-mfcam.sh [--out DIR]...
#   --out DIR   waar rg-mfcam.dll heen gaat (herhaalbaar); standaard patches/MarioKartDX/dropin
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/tools/rg-mfcam/rg_mfcam.cpp"
HDR="$ROOT/src/ravengun/vision/camera_feed_format.h"
CONTAINERFILE="$ROOT/packaging/containers/openparrot/Containerfile"
OUTS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUTS+=("$2"); shift 2 ;;
    -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
done
[[ ${#OUTS[@]} -gt 0 ]] || OUTS=("$ROOT/patches/MarioKartDX/dropin")

IMAGE="localhost/ravengun-openparrot-build:$(sha256sum "$CONTAINERFILE" | cut -c1-12)"
if ! podman image exists "$IMAGE"; then
  echo "Image $IMAGE bouwen (eerste keer)..."
  podman build -q -t "$IMAGE" -f "$CONTAINERFILE" "$(dirname "$CONTAINERFILE")" >/dev/null
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/src/tools/rg-mfcam" "$TMP/src/src/ravengun/vision" "$TMP/out"
cp "$SRC" "$ROOT/tools/rg-mfcam/jvs_com.h" "$ROOT/tools/rg-mfcam/sharek_fake.h" "$ROOT/tools/rg-mfcam/stall_dump.h" "$ROOT/tools/rg-mfcam/drive_fix.h" "$TMP/src/tools/rg-mfcam/"
cp "$HDR" "$TMP/src/src/ravengun/vision/"
# Statisch gelinkt (geen libstdc++/libgcc-DLL's nodig in het spelproces); geen tijdstempel voor reproduceerbaarheid.
podman run --rm --network=none -v "$TMP/src:/src:ro" -v "$TMP/out:/out" "$IMAGE" \
  i686-w64-mingw32-g++ -std=c++17 -O2 -s -shared -Wall -Wextra -Werror \
  -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp -Wl,--enable-stdcall-fixup \
  /src/tools/rg-mfcam/rg_mfcam.cpp -o /out/rg-mfcam.dll -lmfplat -lmfuuid -lole32 -luuid

for o in "${OUTS[@]}"; do
  mkdir -p "$o"
  cp "$TMP/out/rg-mfcam.dll" "$o/rg-mfcam.dll"
  echo "klaar: $o/rg-mfcam.dll"
done
echo "bron: $(sha256sum "$SRC" | cut -c1-16) $(basename "$SRC"); dll: $(sha256sum "$TMP/out/rg-mfcam.dll" | cut -c1-16)"
