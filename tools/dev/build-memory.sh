#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Bouwt de geheugen-motor van RavenGun uit memory/src/.
#
# Dit is het tweede deel van de patchmotor. Het eerste deel (tools/dev/patchfile.py +
# tools/dev/addrmap.py) schrijft in een BESTAND; dit deel schrijft in het GEHEUGEN van een lopend
# proces. Zie docs/patchmotor.md, docs/memory-engine.md en docs/community-model.md paragraaf 3.
#
# Resultaat:
#
#   in de bibliotheekmap (standaard ~/.local/share/ravengun/memory/):
#     ravengun-patchmem.so      64-bits, voor de ELF-games (LD_PRELOAD)
#     ravengun-patchmem32.so    32-bits, voor de ELF-games (LD_PRELOAD)
#     ravengun-patchmem.dll     x86_64, voor de games onder Proton/Wine
#     ravengun-patchmem32.dll   i686,   voor de games onder Proton/Wine
#
#   in memory/out/ (niet geinstalleerd; alleen voor de proeven):
#     probe_linux, libravengun-proeflaat.so, probe_win.exe, probe_win32.exe,
#     ravengun-laat.dll, ravengun-laat32.dll
#
# De Windows-kant wordt gebouwd met mingw-w64 in de rootless podman-image
# localhost/ravengun-mingw (zie hasp-README.md). Ontbreekt die image, dan worden de PE-DLL's
# overgeslagen met een melding -- de Linux-kant werkt dan gewoon.
#
# Er wordt NOOIT iets buiten de bibliotheekmap en memory/out/ gezet. Dit script raakt geen game aan
# en schrijft niets naar de NAS: de motor werkt in procesgeheugen, niet in bestanden.
#
# Gebruik:
#   tools/dev/build-memory.sh                alles bouwen + zelftest
#   tools/dev/build-memory.sh --no-test      niet zelftesten
#   tools/dev/build-memory.sh --no-windows   de PE-DLL's overslaan (geen podman nodig)
#   tools/dev/build-memory.sh --no-install   alleen in memory/out/ bouwen
#
# Omgevingsvariabelen:
#   RAVENGUN_MEMORY_LIB_DIR   waar de bibliotheken heen gaan
#   RAVENGUN_MINGW_IMAGE      de podman-image (standaard localhost/ravengun-mingw)
#   CC                        de compiler (standaard gcc)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="$ROOT/memory/src"
TEST="$ROOT/memory/test"
OUTDIR="$ROOT/memory/out"
IMAGE="${RAVENGUN_MINGW_IMAGE:-localhost/ravengun-mingw}"
CC="${CC:-gcc}"

DO_TEST=1
DO_WINDOWS=1
DO_INSTALL=1
while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-test)    DO_TEST=0 ;;
    --no-windows) DO_WINDOWS=0 ;;
    --no-install) DO_INSTALL=0 ;;
    -h|--help)    sed -n '2,50p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
  shift
done

if [[ -n "${RAVENGUN_MEMORY_LIB_DIR:-}" ]]; then
  LIB="$RAVENGUN_MEMORY_LIB_DIR"
else
  LIB="${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/memory"
fi

[[ -d "$SRC" ]] || { echo "geen $SRC" >&2; exit 1; }
mkdir -p "$OUTDIR"
[[ "$DO_INSTALL" == 1 ]] && mkdir -p "$LIB"

# De 32-bits bouw vraagt de dev-header gnu/stubs-32.h. Die ontbreekt in lib32-glibc op SteamOS (het
# is een dev-header, geen runtime-bestand). Net als scripts/build-hasp.sh maken we hem dan zelf aan,
# in memory/src/inc/gnu/, met dezelfde inhoud als gnu/stubs-64.h.
if [[ ! -e /usr/include/gnu/stubs-32.h ]]; then
  mkdir -p "$SRC/inc/gnu"
  if [[ ! -e "$SRC/inc/gnu/stubs-32.h" ]]; then
    {
      echo "/* Lokaal gemaakt door tools/dev/build-memory.sh: gnu/stubs-32.h ontbreekt in lib32-glibc."
      echo "   De inhoud is gelijk aan gnu/stubs-64.h; zie /usr/include/gnu/stubs.h voor de selectie. */"
      echo "#ifdef _LIBC"
      echo " #error Applications may not define the macro _LIBC"
      echo "#endif"
      grep '^#define __stub_' /usr/include/gnu/stubs-64.h || true
    } > "$SRC/inc/gnu/stubs-32.h"
    echo "gemaakt: memory/src/inc/gnu/stubs-32.h (lokaal; de dev-header ontbrak)"
  fi
  INC32=(-I"$SRC/inc")
else
  INC32=()
fi

# ---------------------------------------------------------------- 1. de Linux-bibliotheken
CFLAGS=(-shared -fPIC -O2 -Wall -Wextra)
echo "bouwen: 64-bits -> $OUTDIR/ravengun-patchmem.so"
$CC "${CFLAGS[@]}" -o "$OUTDIR/ravengun-patchmem.so" \
  "$SRC/patchmem.c" "$SRC/entry_linux.c" -lpthread -ldl
echo "bouwen: 32-bits -> $OUTDIR/ravengun-patchmem32.so"
$CC -m32 "${INC32[@]}" "${CFLAGS[@]}" -o "$OUTDIR/ravengun-patchmem32.so" \
  "$SRC/patchmem.c" "$SRC/entry_linux.c" -lpthread -ldl

# De proefprogramma's (niet installeren; die horen bij de tests).
echo "bouwen: proefprogramma's -> $OUTDIR/"
$CC -O2 -Wall -Wextra -o "$OUTDIR/probe_linux" "$TEST/probe_linux.c" -ldl
# Dezelfde proef nog een keer als NIET-PIE programma. Dan is de modulebasis 0, en dat is de lastige
# randvoorwaarde voor het opzoeken van de module (zie patchmem.h bij patchmem_module_base).
$CC -O2 -Wall -Wextra -no-pie -o "$OUTDIR/probe_linux_nopie" "$TEST/probe_linux.c" -ldl
$CC -shared -fPIC -O2 -Wall -Wextra -o "$OUTDIR/libravengun-proeflaat.so" "$TEST/late_module.c"
# Een nagebootste GEPAKTE module: het bestand bevat andere bytes dan het geheugen (packed_module.c).
$CC -shared -fPIC -O2 -Wall -Wextra -o "$OUTDIR/libravengun-gepakt.so" "$TEST/packed_module.c"
# Dezelfde twee proeven nog een keer 32-bits, zodat de 32-bits .so ook echt gedraaid wordt en niet
# alleen gebouwd.
echo "bouwen: proefprogramma's 32-bits -> $OUTDIR/"
$CC -m32 "${INC32[@]}" -O2 -Wall -Wextra -o "$OUTDIR/probe_linux32" "$TEST/probe_linux.c" -ldl
$CC -m32 "${INC32[@]}" -shared -fPIC -O2 -Wall -Wextra \
  -o "$OUTDIR/libravengun-proeflaat32.so" "$TEST/late_module.c"

# ---------------------------------------------------------------- 2. de Windows-DLL's (podman)
WIN_OK=0
if [[ "$DO_WINDOWS" == 1 ]]; then
  if ! command -v podman >/dev/null; then
    echo "let op: podman ontbreekt; de PE-DLL's worden niet gebouwd (--no-windows om deze melding te dempen)" >&2
  elif ! podman image exists "$IMAGE"; then
    echo "let op: de image $IMAGE bestaat niet; de PE-DLL's worden niet gebouwd." >&2
    echo "        Bouw hem met de opdracht uit hasp-README.md (podman build)." >&2
  else
    echo "bouwen: PE-DLL's en de Windows-proef (mingw-w64 in podman) -> $OUTDIR/"
    podman run --rm -v "$ROOT/memory:/w:Z" -w /w "$IMAGE" bash -lc '
      set -e
      mkdir -p out
      # De motor zelf, beide breedtes. Alleen kernel32; geen Detours.
      x86_64-w64-mingw32-gcc -shared -O2 -Wall -Wextra -o out/ravengun-patchmem.dll   src/patchmem.c src/entry_win.c
      i686-w64-mingw32-gcc   -shared -O2 -Wall -Wextra -o out/ravengun-patchmem32.dll src/patchmem.c src/entry_win.c
      # De module die pas tijdens het draaien geladen wordt.
      x86_64-w64-mingw32-gcc -shared -O2 -Wall -Wextra -o out/ravengun-laat.dll   test/late_module_win.c
      i686-w64-mingw32-gcc   -shared -O2 -Wall -Wextra -o out/ravengun-laat32.dll test/late_module_win.c
      # De proefprogramma programma importeert de DLL van zijn eigen breedte; dat is nodig omdat
      # WINEDLLOVERRIDES alleen werkt voor een module die het programma werkelijk importeert.
      x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -I src -o out/probe_win.exe \
        test/probe_win.c out/ravengun-patchmem.dll
      i686-w64-mingw32-gcc   -O2 -Wall -Wextra -I src -DRAVENGUN_LAAT_DLL=\"ravengun-laat32.dll\" \
        -o out/probe_win32.exe test/probe_win.c out/ravengun-patchmem32.dll
      # Dezelfde proef, maar ZONDER de DLL te importeren. Daarmee is te meten of een andere laadweg
      # werkt (AppInit_DLLs); WINEDLLOVERRIDES doet namelijk alleen iets voor een import.
      x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -DRAVENGUN_GEEN_IMPORT \
        -o out/probe_win_geenimport.exe test/probe_win.c
      ls -l out/*.dll out/*.exe
    '
    WIN_OK=1
  fi
fi

# ---------------------------------------------------------------- 3. installeren
if [[ "$DO_INSTALL" == 1 ]]; then
  install -m 0755 "$OUTDIR/ravengun-patchmem.so"   "$LIB/ravengun-patchmem.so"
  install -m 0755 "$OUTDIR/ravengun-patchmem32.so" "$LIB/ravengun-patchmem32.so"
  if [[ "$WIN_OK" == 1 ]]; then
    install -m 0644 "$OUTDIR/ravengun-patchmem.dll"   "$LIB/ravengun-patchmem.dll"
    install -m 0644 "$OUTDIR/ravengun-patchmem32.dll" "$LIB/ravengun-patchmem32.dll"
  fi
  echo
  echo "geinstalleerd in $LIB:"
  ls -l "$LIB"/ravengun-patchmem* 2>/dev/null || true
else
  echo
  echo "niet geinstalleerd (--no-install); alles staat in $OUTDIR"
fi

# ---------------------------------------------------------------- 4. de zelftest
if [[ "$DO_TEST" == 1 ]]; then
  echo
  echo "zelftest (Linux, 64-bits, via LD_PRELOAD):"
  proef="$OUTDIR/probe_linux"

  # Eerst zonder de motor: dat is de nulmeting.
  voor="$("$proef" zelf)"
  module="$(printf '%s\n' "$voor" | sed -n 's/^module=//p')"
  doel_off="$(printf '%s\n' "$voor" | sed -n 's/^doel_offset=//p')"
  doel_voor="$(printf '%s\n' "$voor" | sed -n 's/^doel=//p')"
  echo "  zonder de motor: $module+$doel_off = $doel_voor"

  lijst="$OUTDIR/zelftest.txt"
  {
    echo "# zelftest van tools/dev/build-memory.sh -- gegenereerd, niet met de hand bijgehouden"
    echo "$module|$doel_off|${doel_voor:0:8}|deadbeefdeadbeefdeadbeefdeadbeef"
  } > "$lijst"

  na="$(RAVENGUN_PATCHMEM_FILE="$lijst" RAVENGUN_PATCHMEM_VERBOSE=1 \
        LD_PRELOAD="$OUTDIR/ravengun-patchmem.so" "$proef" zelf 2>&1)"
  echo "$na" | sed 's/^/  /'

  if printf '%s\n' "$na" | grep -q '^doel=deadbeefdeadbeefdeadbeefdeadbeef$'; then
    echo "  OK: de bytes in het lopende proces zijn veranderd"
  else
    echo "  MISLUKT: de bytes zijn niet veranderd" >&2
    exit 1
  fi

  # En de controle: met een `verwacht` dat niet klopt mag er NIETS geschreven worden. De echte
  # bytes zijn 11223344..., dus we zetten er expres iets anders neer.
  {
    echo "$module|$doel_off|99999999|ffffffffffffffffffffffffffffffff"
  } > "$OUTDIR/zelftest-fout.txt"
  fout="$(RAVENGUN_PATCHMEM_FILE="$OUTDIR/zelftest-fout.txt" \
          LD_PRELOAD="$OUTDIR/ravengun-patchmem.so" "$proef" zelf 2>&1)"
  if printf '%s\n' "$fout" | grep -q '^doel=1122334455667788'; then
    echo "  OK: een verwacht dat niet klopt laat de bytes met rust"
  else
    echo "  MISLUKT: er is geschreven terwijl verwacht niet klopte" >&2
    exit 1
  fi
  printf '%s\n' "$fout" | grep 'GEWEIGERD' | sed 's/^/  /'
fi

echo
if [[ "$DO_INSTALL" == 1 ]]; then
  echo "klaar. De motor staat in $LIB."
else
  echo "klaar. De motor staat in $OUTDIR (niet geinstalleerd)."
fi
echo "Volledige proeven (ook Wine): python3 tests/test_memory_engine.py"
echo "Het formaat en de grenzen: docs/memory-engine.md"
