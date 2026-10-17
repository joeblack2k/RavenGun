#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Bouwt rg-amhw.dll: de kastapparaten (\\.\columba, \\.\mxsram) van een Sega Ring-kast voor een 32-bits Windows-spel onder Wine
# (tools/rg-amhw/rg_amhw.cpp, docs/research/segajvs/README.md). MinGW-w64 (i686) in de gepinde podman-container van de OpenParrot-build,
# zonder netwerk. Het resultaat is een drop-in (docs/dropins.md) voor LGI, LGI3D, LGS, GG, OG, SDR en Transformers.
#
# Gebruik: tools/dev/build-amhw.sh [--out DIR]...     (standaard build-amhw/ in de repo)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CONTAINERFILE="$ROOT/packaging/containers/openparrot/Containerfile"
OUTS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUTS+=("$2"); shift 2 ;;
    -h|--help) sed -n '2,8p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
done
[[ ${#OUTS[@]} -gt 0 ]] || OUTS=("$ROOT/build-amhw")
IMAGE="localhost/ravengun-openparrot-build:$(sha256sum "$CONTAINERFILE" | cut -c1-12)"
if ! podman image exists "$IMAGE"; then
  echo "Image $IMAGE bouwen (eerste keer)..."
  podman build -q -t "$IMAGE" -f "$CONTAINERFILE" "$(dirname "$CONTAINERFILE")" >/dev/null
fi
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/src" "$TMP/out"
cp "$ROOT/tools/rg-amhw/rg_amhw.cpp" "$ROOT/tools/rg-amhw/amhw_dmi.h" "$ROOT/tools/rg-amhw/amhw_jvs.h" "$ROOT/tools/rg-amhw/amhw_table.h" "$ROOT/tools/rg-amhw/amhw_smbus.h" "$ROOT/tools/rg-amhw/amhw_sysdata.h" "$TMP/src/"
podman run --rm --network=none -v "$TMP/src:/src:ro" -v "$TMP/out:/out" "$IMAGE" \
  i686-w64-mingw32-g++ -std=c++17 -O2 -s -shared -Wall -Wextra -Werror \
  -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp \
  /src/rg_amhw.cpp -o /out/rg-amhw.dll -lsetupapi
for o in "${OUTS[@]}"; do
  mkdir -p "$o"
  cp "$TMP/out/rg-amhw.dll" "$o/rg-amhw.dll"
  echo "klaar: $o/rg-amhw.dll"
done
echo "bron: $(cat "$ROOT/tools/rg-amhw/rg_amhw.cpp" "$ROOT/tools/rg-amhw/amhw_dmi.h" "$ROOT/tools/rg-amhw/amhw_jvs.h" "$ROOT/tools/rg-amhw/amhw_table.h" "$ROOT/tools/rg-amhw/amhw_smbus.h" "$ROOT/tools/rg-amhw/amhw_sysdata.h" | sha256sum | cut -c1-16); dll: $(sha256sum "$TMP/out/rg-amhw.dll" | cut -c1-16)"
