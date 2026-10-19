#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Bouwt rg-rhio.dll: de I/O-bordkant van RavenGun (tools/rg-rhio/rg_rhio.cpp) voor de 32-bits
# Wartran-spellen (Robin Hood). Zelfde vorm als tools/dev/build-mfcam.sh: de kruiscompiler zit in de
# rootless podman-image localhost/ravengun-mingw.
#
# Gebruik:
#   tools/dev/build-rhio.sh                 bouwen en naar de drop-in van Robin Hood kopieren
#   tools/dev/build-rhio.sh --out DIR       ook naar DIR kopieren (herhaalbaar)
#   RAVENGUN_MINGW_IMAGE=<image>            een andere image gebruiken
#
# Waarom een script en geen CMake-doel: alle Windows-drop-ins van dit project worden zo gebouwd
# (CMake bouwt de Linux-kant en de proeven). Er is wel een CMake-doel `rg-rhio` dat dit script
# aanroept, zodat je het met `cmake --build build --target rg-rhio` kunt bouwen.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="$ROOT/tools/rg-rhio"
IMAGE="${RAVENGUN_MINGW_IMAGE:-localhost/ravengun-mingw}"
OUT=("$ROOT/patches/RobinHood/dropin")

while [ $# -gt 0 ]; do
    case "$1" in
        --out) OUT+=("$2"); shift 2 ;;
        *) echo "onbekende optie: $1" >&2; exit 2 ;;
    esac
done

if ! command -v podman >/dev/null 2>&1; then
    echo "rg-rhio: podman ontbreekt; de DLL kan niet gebouwd worden (de rest werkt)" >&2
    exit 77
fi

podman run --rm -v "$SRC:/w:Z" -w /w "$IMAGE" bash -lc '
    set -e
    mkdir -p out
    i686-w64-mingw32-g++ -shared -O2 -Wall -static -s -o out/rg-rhio.dll rg_rhio.cpp -Wl,--kill-at
    ls -l out/rg-rhio.dll
'

for o in "${OUT[@]}"; do
    mkdir -p "$o"
    cp "$SRC/out/rg-rhio.dll" "$o/rg-rhio.dll"
    echo "gekopieerd naar $o/rg-rhio.dll"
done
echo "klaar."
