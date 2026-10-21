#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds rg-mfcam.dll: RavenGun's Media Foundation camera for a 32-bit (or, with --arch x86_64, 64-bit) Windows game under Wine (tools/rg-mfcam/rg_mfcam.cpp;
# the camera image comes from the daemon, src/ravengun/vision/camera_feed.hpp). MinGW-w64 (i686) in the pinned podman container of the
# OpenParrot build, without network; i686 or x86_64. The result is a drop-in (docs/dropins.md) and belongs in the folder of every game that uses it.
#
# Usage: tools/dev/build-mfcam.sh [--arch i686|x86_64] [--out DIR]...
#   --arch A    i686 (default; Mario Kart DX) or x86_64 (Lost Land Adventure's launcher)
#   --out DIR   where rg-mfcam.dll goes (repeatable); default patches/MarioKartDX/dropin for i686,
#               patches/LostLandAdventure/dropin for x86_64
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/tools/rg-mfcam/rg_mfcam.cpp"
HDR="$ROOT/src/ravengun/vision/camera_feed_format.h"
CONTAINERFILE="$ROOT/packaging/containers/openparrot/Containerfile"
OUTS=()
ARCH=i686
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUTS+=("$2"); shift 2 ;;
    --arch) ARCH="$2"; shift 2 ;;
    -h|--help) sed -n '2,11p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
case "$ARCH" in
  i686) DEFAULT_OUT="$ROOT/patches/MarioKartDX/dropin"; ARCH_FLAGS=(-Wl,--enable-stdcall-fixup) ;;
  x86_64) DEFAULT_OUT="$ROOT/patches/LostLandAdventure/dropin"; ARCH_FLAGS=() ;;
  *) echo "unknown --arch: $ARCH (i686 or x86_64)" >&2; exit 2 ;;
esac
[[ ${#OUTS[@]} -gt 0 ]] || OUTS=("$DEFAULT_OUT")

IMAGE="localhost/ravengun-openparrot-build:$(sha256sum "$CONTAINERFILE" | cut -c1-12)"
if ! podman image exists "$IMAGE"; then
  echo "Building image $IMAGE (first time)..."
  podman build -q -t "$IMAGE" -f "$CONTAINERFILE" "$(dirname "$CONTAINERFILE")" >/dev/null
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/src/tools/rg-mfcam" "$TMP/src/src/ravengun/vision" "$TMP/out"
cp "$SRC" "$ROOT/tools/rg-mfcam/jvs_com.h" "$ROOT/tools/rg-mfcam/sharek_fake.h" "$ROOT/tools/rg-mfcam/am_auth_fake.h" "$ROOT/tools/rg-mfcam/stall_dump.h" "$ROOT/tools/rg-mfcam/drive_fix.h" "$ROOT/tools/rg-mfcam/bngrw_fake.h" "$ROOT/tools/rg-mfcam/window_mode.h" "$ROOT/tools/rg-mfcam/str_board.h" "$ROOT/tools/rg-mfcam/usb_dongle.h" "$ROOT/tools/rg-mfcam/content_router.h" "$ROOT/tools/rg-mfcam/cam_select.h" "$ROOT/tools/rg-mfcam/dome_gun.h" "$TMP/src/tools/rg-mfcam/"
cp "$HDR" "$TMP/src/src/ravengun/vision/"
# Statically linked (no libstdc++/libgcc DLLs needed in the game process); no timestamp, for reproducibility.
podman run --rm --network=none -v "$TMP/src:/src:ro" -v "$TMP/out:/out" "$IMAGE" \
  "$ARCH-w64-mingw32-g++" -std=c++17 -O2 -s -shared -Wall -Wextra -Werror \
  -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp "${ARCH_FLAGS[@]}" \
  /src/tools/rg-mfcam/rg_mfcam.cpp -o /out/rg-mfcam.dll -lmfplat -lmfuuid -lole32 -luuid -lsetupapi -lcfgmgr32

for o in "${OUTS[@]}"; do
  mkdir -p "$o"
  cp "$TMP/out/rg-mfcam.dll" "$o/rg-mfcam.dll"
  echo "done: $o/rg-mfcam.dll"
done
echo "source: $(sha256sum "$SRC" | cut -c1-16) $(basename "$SRC"); $ARCH dll: $(sha256sum "$TMP/out/rg-mfcam.dll" | cut -c1-16)"
