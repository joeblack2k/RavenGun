#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Bouwt rg-dinput8 (tools/rg-dinput8/: RavenGuns eigen dinput8.dll, 32-bits, alleen kernel32 en msvcrt) met de eigen
# mingw-w64-sysroot (zoals tools/rg-pqmt/build.sh) of, als die er niet is, in de gepinde podman-container van de OpenParrot-build
# zonder netwerk (zoals tools/dev/build-adrio.sh). Schrijft dinput8.dll en BUILDINFO-dinput8.txt in elke --out-map.
#
# Gebruik: tools/rg-dinput8/build.sh [--out MAP]... [--as dinput.dll]   standaard build/rg-dinput8/; MAP is meestal patches/<Game>/dropin
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUTS=()
NAME=dinput8.dll
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUTS+=("$2"); shift 2 ;;
    --as) NAME="$2"; shift 2 ;;   # file name of the copy in --out (dinput.dll for a game that imports DINPUT.dll)
    -h|--help) sed -n '3,8p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
done
[[ ${#OUTS[@]} -gt 0 ]] || OUTS=("$ROOT/build/rg-dinput8")
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
FLAGS=(-shared -O2 -s -std=c11 -Wall -Wextra -Werror -Wno-cast-function-type -static-libgcc
       -o /out/dinput8.dll /src/rg_dinput8.c /src/rg_dinput8_kern.c
       -Wl,--kill-at -Wl,--no-insert-timestamp -Wl,--enable-stdcall-fixup)
SR="${RG_MINGW_SYSROOT:-$HOME/.cache/ravengun-mingw/sysroot}"
if [[ -x "$SR/usr/bin/i686-w64-mingw32-gcc" ]]; then
  GCCV="$(ls "$SR/usr/lib/gcc/i686-w64-mingw32/" | head -1)"
  args=("${FLAGS[@]}")
  args=("${args[@]//\/src/$HERE}")
  args=("${args[@]//\/out/$TMP}")
  "$SR/usr/bin/i686-w64-mingw32-gcc" -B"$SR/usr/lib/gcc/i686-w64-mingw32/$GCCV/" -B"$SR/usr/i686-w64-mingw32/" \
    -B"$SR/usr/i686-w64-mingw32/lib/" -isystem "$SR/usr/i686-w64-mingw32/include" -L"$SR/usr/i686-w64-mingw32/lib" "${args[@]}"
  TOOL="$("$SR/usr/bin/i686-w64-mingw32-gcc" --version | head -1) (sysroot $SR)"
else
  CONTAINERFILE="$ROOT/packaging/containers/openparrot/Containerfile"
  IMAGE="localhost/ravengun-openparrot-build:$(sha256sum "$CONTAINERFILE" | cut -c1-12)"
  podman image exists "$IMAGE" || podman build -q -t "$IMAGE" -f "$CONTAINERFILE" "$(dirname "$CONTAINERFILE")" >/dev/null
  podman run --rm --network=none -v "$HERE:/src:ro" -v "$TMP:/out" "$IMAGE" i686-w64-mingw32-gcc "${FLAGS[@]}"
  TOOL="$(podman run --rm --network=none "$IMAGE" i686-w64-mingw32-gcc --version | head -1) (container $IMAGE)"
fi
{
  echo "dinput8.dll = rg-dinput8 (GPL-3.0), RavenGuns eigen DirectInput8 met de geweren als joysticks"
  echo "bron:"; (cd "$ROOT" && sha256sum tools/rg-dinput8/rg_dinput8.c tools/rg-dinput8/rg_dinput8_kern.c tools/rg-dinput8/rg_dinput8_kern.h | sed 's/^/  /')
  echo "toolchain: $TOOL"
  echo "uitvoer:"; (cd "$TMP" && sha256sum dinput8.dll | sed 's/^/  /')
} > "$TMP/BUILDINFO-dinput8.txt"
for o in "${OUTS[@]}"; do
  mkdir -p "$o"
  cp "$TMP/dinput8.dll" "$o/$NAME"
  cp "$TMP/BUILDINFO-dinput8.txt" "$o/"
  echo "klaar: $o/$NAME"
done
