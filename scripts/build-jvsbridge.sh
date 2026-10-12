#!/usr/bin/env bash
# Bouwt rg-jvsbridge.exe: RavenGuns eigen vervanger voor TeknoParrots pipehelper.exe (de JVS-named-pipe +
# /dev/shm-brug). Clean-room, uit third_party/rg-jvsbridge/rg_jvsbridge.c (GPL-3.0), met de MinGW-w64-toolchain
# in de gepinde podman-container van de OpenParrot-build. Geen netwerk tijdens het bouwen.
#
# Gebruik: scripts/build-jvsbridge.sh [--out DIR]
#   --out DIR   uitvoer (standaard ~/.local/share/ravengun/jvsbridge/; $RAVENGUN_JVSBRIDGE_OUT)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/third_party/rg-jvsbridge/rg_jvsbridge.c"
CONTAINERFILE="$ROOT/packaging/containers/openparrot/Containerfile"
OUT="${RAVENGUN_JVSBRIDGE_OUT:-${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/jvsbridge}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUT="$2"; shift 2 ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
done

IMAGE="localhost/ravengun-openparrot-build:$(sha256sum "$CONTAINERFILE" | cut -c1-12)"
if ! podman image exists "$IMAGE"; then
  echo "Image $IMAGE bouwen (eerste keer)..."
  podman build -q -t "$IMAGE" -f "$CONTAINERFILE" "$(dirname "$CONTAINERFILE")" >/dev/null
fi

mkdir -p "$OUT"
# -municode is niet nodig (gewone main); console-subsysteem; geen tijdstempel voor reproduceerbaarheid.
podman run --rm --network=none -v "$ROOT/third_party/rg-jvsbridge:/src:ro" -v "$OUT:/out" "$IMAGE" \
  x86_64-w64-mingw32-gcc -O2 -s -static -Wall \
  -Wl,--subsystem,console -Wl,--no-insert-timestamp \
  /src/rg_jvsbridge.c -o /out/rg-jvsbridge.exe -lws2_32

{
  echo "rg-jvsbridge — RavenGuns pipehelper-vervanger (clean-room uit de gedragsspec, GPL-3.0)"
  echo "gebouwd: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "bron:"; sha256sum "$SRC" | sed "s#$ROOT/##;s/^/  /"
  echo "toolchain: $(podman run --rm --network=none "$IMAGE" x86_64-w64-mingw32-gcc --version | head -1)"
  echo "uitvoer:"; (cd "$OUT" && sha256sum rg-jvsbridge.exe | sed 's/^/  /')
} > "$OUT/BUILDINFO.txt"
echo "klaar: $OUT/rg-jvsbridge.exe"
cat "$OUT/BUILDINFO.txt"
