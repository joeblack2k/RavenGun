#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds rg-mfcam.dll: RavenGun's Media Foundation camera for a 32-bit Windows game under Wine (tools/rg-mfcam/rg_mfcam.cpp;
# the camera image comes from the daemon, src/ravengun/vision/camera_feed.hpp). MinGW-w64 (i686) in the pinned podman container of the
# OpenParrot build, without network. The result is a drop-in (docs/dropins.md) and belongs in the folder of every game that uses it.
#
# Usage: tools/dev/build-mfcam.sh [--out DIR]...
#   --out DIR   where rg-mfcam.dll goes (repeatable); default patches/MarioKartDX/dropin
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
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
[[ ${#OUTS[@]} -gt 0 ]] || OUTS=("$ROOT/patches/MarioKartDX/dropin")

IMAGE="localhost/ravengun-openparrot-build:$(sha256sum "$CONTAINERFILE" | cut -c1-12)"
if ! podman image exists "$IMAGE"; then
  echo "Building image $IMAGE (first time)..."
  podman build -q -t "$IMAGE" -f "$CONTAINERFILE" "$(dirname "$CONTAINERFILE")" >/dev/null
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/src/tools/rg-mfcam" "$TMP/src/src/ravengun/vision" "$TMP/out"
cp "$SRC" "$ROOT/tools/rg-mfcam/jvs_com.h" "$ROOT/tools/rg-mfcam/sharek_fake.h" "$ROOT/tools/rg-mfcam/am_auth_fake.h" "$ROOT/tools/rg-mfcam/stall_dump.h" "$ROOT/tools/rg-mfcam/drive_fix.h" "$ROOT/tools/rg-mfcam/bngrw_fake.h" "$ROOT/tools/rg-mfcam/window_mode.h" "$ROOT/tools/rg-mfcam/str_board.h" "$ROOT/tools/rg-mfcam/usb_dongle.h" "$ROOT/tools/rg-mfcam/content_router.h" "$TMP/src/tools/rg-mfcam/"
cp "$HDR" "$TMP/src/src/ravengun/vision/"
# Statically linked (no libstdc++/libgcc DLLs needed in the game process); no timestamp, for reproducibility.
podman run --rm --network=none -v "$TMP/src:/src:ro" -v "$TMP/out:/out" "$IMAGE" \
  i686-w64-mingw32-g++ -std=c++17 -O2 -s -shared -Wall -Wextra -Werror \
  -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp -Wl,--enable-stdcall-fixup \
  /src/tools/rg-mfcam/rg_mfcam.cpp -o /out/rg-mfcam.dll -lmfplat -lmfuuid -lole32 -luuid -lsetupapi -lcfgmgr32

for o in "${OUTS[@]}"; do
  mkdir -p "$o"
  cp "$TMP/out/rg-mfcam.dll" "$o/rg-mfcam.dll"
  echo "done: $o/rg-mfcam.dll"
done
echo "source: $(sha256sum "$SRC" | cut -c1-16) $(basename "$SRC"); dll: $(sha256sum "$TMP/out/rg-mfcam.dll" | cut -c1-16)"
