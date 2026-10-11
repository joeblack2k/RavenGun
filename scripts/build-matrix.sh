#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Bouwt de eigen Matrix/SBK-donglelaag van RavenGun uit matrix/src/.
#
# Resultaat (in matrix/out/):
#   matrix32.dll    i686    (de 32-bits games; onder Proton/Wine gewoon een PE-DLL)
#   matrix64.dll    x86_64  (de 64-bits Unity-games, waaronder Rabbids Hollywood)
#   selftest        native Linux-binary: controleert de teruggeefwaarden zonder Wine
#
# De bron is niet game-specifiek: dezelfde twee DLL's bedienen elke game die de Matrix-API gebruikt.
# Wat een game nodig heeft staat in patches/<Game>/dongle.json en patches/<Game>/dropin/ (docs/dropins.md);
# scripts/matrix.py plaatst ze in de view van de game en rekent de WINEDLLOVERRIDES uit.
#
# De kruiscompiler zit in een rootless podman-image (standaard localhost/ravengun-mingw). Ontbreekt
# die, dan bouwt dit script hem uit ~/mingw-build.
#
# Gebruik:
#   scripts/build-matrix.sh                  bouwen, zelftest, en (indien vindbaar) de ABI vergelijken
#   scripts/build-matrix.sh --no-test        alleen bouwen
#   RAVENGUN_MINGW_IMAGE=<image>             een andere image gebruiken
#   RAVENGUN_MATRIX_ORIGINEEL=<pad>          het originele matrix32.dll voor de ABI-controle
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/matrix/src"
OUT="$ROOT/matrix/out"
IMAGE="${RAVENGUN_MINGW_IMAGE:-localhost/ravengun-mingw}"
MINGW_BUILD_DIR="${RAVENGUN_MINGW_BUILD_DIR:-$HOME/mingw-build}"
DO_TEST=1
[[ "${1:-}" == "--no-test" ]] && DO_TEST=0

[[ -d "$SRC" ]] || { echo "geen $SRC" >&2; exit 1; }
mkdir -p "$OUT"

# ---------------------------------------------------------------- de kruiscompiler
if ! podman image exists "$IMAGE"; then
  echo "image $IMAGE ontbreekt; bouwen uit $MINGW_BUILD_DIR"
  [[ -f "$MINGW_BUILD_DIR/Containerfile" ]] || { echo "geen $MINGW_BUILD_DIR/Containerfile" >&2; exit 1; }
  podman build -t "$IMAGE" "$MINGW_BUILD_DIR"
fi

echo "bouwen: matrix32.dll (i686) en matrix64.dll (x86_64) in $IMAGE"
podman run --rm -v "$SRC:/w:Z" -v "$OUT:/out:Z" -w /w "$IMAGE" bash -lc '
  set -e
  i686-w64-mingw32-gcc   -shared -O2 -Wall -Wextra -o /out/matrix32.dll matrix.c matrix.def -Wl,--kill-at
  x86_64-w64-mingw32-gcc -shared -O2 -Wall -Wextra -o /out/matrix64.dll matrix.c matrix.def -Wl,--kill-at
  i686-w64-mingw32-strip   /out/matrix32.dll
  x86_64-w64-mingw32-strip /out/matrix64.dll
'
ls -l "$OUT"/matrix32.dll "$OUT"/matrix64.dll

# ---------------------------------------------------------------- de zelftest (native, zonder Wine)
if [[ "$DO_TEST" == 1 ]]; then
  echo
  echo "zelftest (native Linux, 64-bits):"
  if command -v gcc > /dev/null; then
    gcc -O2 -Wall -Wextra -o "$OUT/selftest" "$SRC/selftest.c" "$SRC/matrix.c"
    "$OUT/selftest"
  else
    echo "  (gcc ontbreekt; overgeslagen)"
  fi
fi

# ---------------------------------------------------------------- de ABI-controle
# Onze DLL moet niet alleen dezelfde 113 namen exporteren, maar per functie ook dezelfde
# argumentgrootte (`ret N`) als het origineel, anders loopt de stapel van de game scheef.
if [[ "$DO_TEST" == 1 ]]; then
  ORIG="${RAVENGUN_MATRIX_ORIGINEEL:-}"
  if [[ -z "$ORIG" ]]; then
    for kand in \
      "$HOME/mnt/spidernas_emulation/Teknoparrot/Rabbids Hollywood /matrix32.dll" \
      "$HOME/mnt/spidernas_emulation/Teknoparrot/Rampage/matrix32.dll" \
      "$HOME/mnt/spidernas_emulation/Teknoparrot/Tomb Raider/matrix32.dll"; do
      [[ -f "$kand" ]] && ORIG="$kand" && break
    done
  fi
  echo
  if [[ -n "$ORIG" && -f "$ORIG" ]]; then
    echo "ABI-controle tegen het origineel: $ORIG"
    python3 "$SRC/controleer-abi.py" "$OUT/matrix32.dll" "$ORIG" || exit 1
  else
    echo "ABI-controle overgeslagen: geen origineel matrix32.dll gevonden."
    echo "Zet RAVENGUN_MATRIX_ORIGINEEL=<pad> om hem toch te doen."
    python3 "$SRC/controleer-abi.py" "$OUT/matrix32.dll" | head -5
  fi
fi

echo
echo "klaar. Drop-in voor een game: patches/<Game>/dropin/ + dropin.ini (docs/dropins.md)."
echo "Status en plaatsing: scripts/matrix.py --game <Game> --json"
