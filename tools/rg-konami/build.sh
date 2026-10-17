#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Bouwt de eigen Konami-I/O-bibliotheken (32-bit PE) met MinGW-w64 in de gepinde podman-container van de OpenParrot-build, zonder
# netwerk (zelfde opzet als tools/rg-macaw/build.sh):
#   Lethal Enforcers 3: iob.dll + gunio.dll           -> --le3 DIR  (standaard patches/LethalEnforcers3/dropin)
#   Gashaaaan:          iob.dll + ballio.dll          -> --gsh DIR  (standaard patches/Gashaaaan/dropin)
# en schrijft voor Lethal Enforcers 3 ook DIR/SAVE/io.sav (de geweerkalibratie die bij onze ruwe waarden hoort; met de gcc van de
# machine via test_konami.c).
#
# Gebruik: tools/rg-konami/build.sh [--le3 DIR] [--gsh DIR]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
CONTAINERFILE="$ROOT/packaging/containers/openparrot/Containerfile"
LE3="$ROOT/patches/LethalEnforcers3/dropin"
GSH="$ROOT/patches/Gashaaaan/dropin"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --le3) LE3="$2"; shift 2 ;;
    --gsh) GSH="$2"; shift 2 ;;
    -h|--help) sed -n '2,11p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
done
IMAGE="localhost/ravengun-openparrot-build:$(sha256sum "$CONTAINERFILE" | cut -c1-12)"
podman image exists "$IMAGE" || { echo "image $IMAGE ontbreekt (bouw eerst met scripts/build-openparrot.sh)" >&2; exit 1; }
mkdir -p "$LE3" "$GSH"
LE3="$(cd "$LE3" && pwd)"
GSH="$(cd "$GSH" && pwd)"
# Zonder C-runtime (zoals tools/rg-pqmt): alleen kernel32 en user32, eigen DllMain als ingang, geen stack-protector of -probe;
# -fno-tree-loop-distribute-patterns zodat gcc de eigen memset/memcpy (kb_win.h) niet in een aanroep van zichzelf omzet.
# --no-insert-timestamp: dezelfde bron geeft dezelfde bytes.
FLAGS="-std=c11 -shared -O2 -Wall -Wextra -Werror -Wno-cast-function-type -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -fno-stack-protector \
  -fno-asynchronous-unwind-tables -fno-ident -mno-stack-arg-probe -nostdlib -nostartfiles \
  -Wl,-e,_DllMain@12 -Wl,--no-insert-timestamp -Wl,--enable-stdcall-fixup -s"
LIBS="-lkernel32 -luser32"
podman run --rm --network=none -v "$HERE:/src:ro" -v "$LE3:/le3" -v "$GSH:/gsh" "$IMAGE" sh -c "
  set -e
  CC=i686-w64-mingw32-gcc
  \$CC $FLAGS -o /le3/iob.dll    /src/rg_le3_iob.c    /src/le3_iob.def $LIBS
  \$CC $FLAGS -o /le3/gunio.dll  /src/rg_le3_gunio.c  /src/le3_gunio.def $LIBS
  \$CC $FLAGS -o /gsh/iob.dll    /src/rg_gsh_iob.c    /src/gsh_iob.def $LIBS
  \$CC $FLAGS -o /gsh/ballio.dll /src/rg_gsh_ballio.c /src/gsh_ballio.def $LIBS"
# io.sav voor Lethal Enforcers 3, uit dezelfde code als de test (le3_maak_iosav in konami_logic.h).
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cc -std=c11 -O2 -Wall -Wextra -o "$TMP/test_konami" "$HERE/test_konami.c" -lm
mkdir -p "$LE3/SAVE"
"$TMP/test_konami" --schrijf-iosav "$LE3/SAVE/io.sav"
( cd "$LE3" && sha256sum iob.dll gunio.dll SAVE/io.sav )
( cd "$GSH" && sha256sum iob.dll ballio.dll )
