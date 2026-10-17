#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Bouwt rg-kaboom (tools/rg-kaboom/: de eigen KaboomAdrioFxPlusFourFeeders64.dll voor de Kaboom/Adrio-games van Adrenaline, Rampage)
# met MinGW-w64 in de gepinde podman-container van de OpenParrot-build, zonder netwerk. Zelfde opzet als tools/dev/build-pe-hook.sh.
#
# Gebruik: tools/dev/build-kaboom.sh [--out MAP]...      standaard build/rg-kaboom/; MAP is meestal patches/<Game>/dropin
# De DLL heet in de uitvoer KaboomAdrioFxPlusFourFeeders64.dll (de naam die de game laadt) en komt met BUILDINFO-kaboom.txt.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/tools/rg-kaboom"
CONTAINERFILE="$ROOT/packaging/containers/openparrot/Containerfile"
OUTS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUTS+=("$2"); shift 2 ;;
    -h|--help) sed -n '3,7p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
done
[[ ${#OUTS[@]} -gt 0 ]] || OUTS=("$ROOT/build/rg-kaboom")
IMAGE="localhost/ravengun-openparrot-build:$(sha256sum "$CONTAINERFILE" | cut -c1-12)"
if ! podman image exists "$IMAGE"; then
  echo "Image $IMAGE bouwen (eerste keer)..."
  podman build -q -t "$IMAGE" -f "$CONTAINERFILE" "$(dirname "$CONTAINERFILE")" >/dev/null
fi
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
podman run --rm --network=none -v "$SRC:/src:ro" -v "$TMP:/out" "$IMAGE" \
  x86_64-w64-mingw32-gcc -shared -O2 -s -std=c11 -Wall -Wextra -Werror -static-libgcc \
  -o /out/KaboomAdrioFxPlusFourFeeders64.dll /src/rg_kaboom.c /src/rg_kaboom_kern.c \
  -Wl,--no-insert-timestamp
{
  echo "KaboomAdrioFxPlusFourFeeders64.dll = rg-kaboom (GPL-3.0), RavenGuns eigen Kaboom/Adrio-bord"
  echo "bron:"; (cd "$ROOT" && sha256sum tools/rg-kaboom/rg_kaboom.c tools/rg-kaboom/rg_kaboom_kern.c tools/rg-kaboom/rg_kaboom_kern.h | sed 's/^/  /')
  echo "toolchain: $(podman run --rm --network=none "$IMAGE" x86_64-w64-mingw32-gcc --version | head -1)"
  echo "uitvoer:"; (cd "$TMP" && sha256sum KaboomAdrioFxPlusFourFeeders64.dll | sed 's/^/  /')
} > "$TMP/BUILDINFO-kaboom.txt"
for o in "${OUTS[@]}"; do
  mkdir -p "$o"
  cp "$TMP/KaboomAdrioFxPlusFourFeeders64.dll" "$TMP/BUILDINFO-kaboom.txt" "$o/"
  echo "klaar: $o/KaboomAdrioFxPlusFourFeeders64.dll"
done
