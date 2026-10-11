#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Bouwt de eigen HASP-emulator van RavenGun uit hasp/src/.
#
# Twee doelen:
#
#   1. de Linux-bibliotheek, voor de ELF-games (LD_PRELOAD):
#        <bibliotheekmap>/ravengun-hasp.so        32-bits
#        <bibliotheekmap>/ravengun-hasp-x64.so    64-bits
#
#   2. de Windows-DLL, voor de games onder Proton:
#        <bibliotheekmap>/windows/hasp_windows_x64.dll
#        <bibliotheekmap>/windows/hasp_windows_x64_105293.dll
#        <bibliotheekmap>/windows/hasp_windows_x64_106993.dll
#        <bibliotheekmap>/windows/hasp_windows_32.dll
#      Gebouwd met mingw-w64 in de rootless podman-image localhost/ravengun-mingw. De namen en de
#      ordinalen komen uit hasp/src/hasp_windows.def en zijn nagemeten op de runtime van de fabrikant
#      (docs/hasp-layer.md).
#
# Daarna zet dit script de gebouwde bestanden in de drop-in-map van elke game die ze noemt
# (patches/<Game>/dropin/dropin.ini; docs/dropins.md). Dat is datagestuurd: een drop-in toevoegen is
# een bestand in dropin/ zetten plus een sectie in dropin.ini, en dit script vindt hem vanzelf.
# Zonder --no-dropin wordt er nooit iets buiten patches/<Game>/dropin/ gezet.
#
# De DONGLEGEGEVENS van een game staan niet hier maar in patches/<Game>/dongle.json + dongle/.
#
# De bibliotheekmap (waar de emulator staat, niet de dongles):
#   RAVENGUN_HASP_LIB_DIR, anders RAVENGUN_USER_HASP_DIR, anders ${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/hasp
# Let op: RAVENGUN_HASP_DIR is voor de emulator de wortel van de DONGLEGEGEVENS (standaard /hasp), niet de
# bibliotheekmap. Zo kan dezelfde variabele voor de Linux- en de Windows-laag gebruikt worden.
#
# Gebruik:
#   scripts/build-hasp.sh                  alles bouwen, zelftest, drop-ins installeren
#   scripts/build-hasp.sh --no-test        niet zelftesten
#   scripts/build-hasp.sh --no-windows     de PE-DLL overslaan (geen podman nodig)
#   scripts/build-hasp.sh --no-dropin      niets in patches/<Game>/dropin/ zetten
#   scripts/build-hasp.sh --dropin <Game>  alleen de drop-ins van deze game installeren
#
# Let op: de 32-bits bouw vraagt de dev-header gnu/stubs-32.h. Die ontbreekt in lib32-glibc op SteamOS
# (het is een dev-header, geen runtime-bestand). Dit script maakt hem zo nodig zelf aan in hasp/src/inc/gnu/,
# met dezelfde inhoud als gnu/stubs-64.h -- dezelfde verouderde BSD-functies zijn op elke architectuur een
# stub. Dat is genoeg om te compileren en te linken tegen de 32-bits libc die er wel is (/usr/lib32/libc.so.6).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/hasp/src"
CC="${CC:-gcc}"
DO_TEST=1
DO_WINDOWS=1
DO_DROPIN=1
ONLY_GAME=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-test)    DO_TEST=0 ;;
    --no-windows) DO_WINDOWS=0 ;;
    --no-dropin)  DO_DROPIN=0 ;;
    --dropin)     ONLY_GAME="${2:?--dropin vereist een gamenaam}"; shift ;;
    -h|--help)    sed -n '2,45p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
  shift
done

if [[ -n "${RAVENGUN_HASP_LIB_DIR:-}" ]]; then
  OUT="$RAVENGUN_HASP_LIB_DIR"
elif [[ -n "${RAVENGUN_USER_HASP_DIR:-}" ]]; then
  OUT="$RAVENGUN_USER_HASP_DIR"
else
  OUT="${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/hasp"
fi

[[ -d "$SRC" ]] || { echo "geen $SRC" >&2; exit 1; }
mkdir -p "$OUT"

# De 32-bits dev-header, alleen als hij ontbreekt.
if [[ ! -e /usr/include/gnu/stubs-32.h ]]; then
  mkdir -p "$SRC/inc/gnu"
  if [[ ! -e "$SRC/inc/gnu/stubs-32.h" ]]; then
    {
      echo "/* Lokaal gemaakt door scripts/build-hasp.sh: gnu/stubs-32.h ontbreekt in lib32-glibc."
      echo "   De inhoud is gelijk aan gnu/stubs-64.h; zie /usr/include/gnu/stubs.h voor de selectie. */"
      echo "#ifdef _LIBC"
      echo " #error Applications may not define the macro _LIBC"
      echo "#endif"
      grep '^#define __stub_' /usr/include/gnu/stubs-64.h || true
    } > "$SRC/inc/gnu/stubs-32.h"
    echo "gemaakt: hasp/src/inc/gnu/stubs-32.h (lokaal; de dev-header ontbrak)"
  fi
  INC32=(-I"$SRC/inc")
else
  INC32=()
fi

# ------------------------------------------------------------------ 1. de Linux-bibliotheek
CFLAGS=(-shared -fPIC -O2 -Wall -Wextra)
echo "bouwen: 32-bits -> $OUT/ravengun-hasp.so"
$CC -m32 "${INC32[@]}" "${CFLAGS[@]}" -o "$OUT/ravengun-hasp.so" "$SRC/hasp_preload.c" "$SRC/dongle.c"
echo "bouwen: 64-bits -> $OUT/ravengun-hasp-x64.so"
$CC "${CFLAGS[@]}" -o "$OUT/ravengun-hasp-x64.so" "$SRC/hasp_preload.c" "$SRC/dongle.c"

# ------------------------------------------------------------------ 2. de Windows-DLL (mingw in podman)
BUILT=("ravengun-hasp.so" "ravengun-hasp-x64.so")
WIN_OK=0
if [[ "$DO_WINDOWS" == 1 ]]; then
  if ! command -v podman >/dev/null; then
    echo "let op: podman ontbreekt; de Windows-DLL wordt niet gebouwd (--no-windows om deze melding te dempen)" >&2
  elif ! podman image inspect localhost/ravengun-mingw >/dev/null 2>&1; then
    echo "let op: de image localhost/ravengun-mingw bestaat niet; de Windows-DLL wordt niet gebouwd." >&2
    echo "        Bouw hem met de opdracht uit hasp-README.md (podman build)." >&2
  else
    mkdir -p "$ROOT/hasp/out" "$OUT/windows"
    echo "bouwen: Windows-DLL's (mingw-w64 in podman) -> $OUT/windows/"
    podman run --rm -v "$ROOT/hasp:/w:Z" -v "$ROOT/tests:/t:Z,ro" -w /w localhost/ravengun-mingw bash -lc '
      set -e
      mkdir -p out
      # Per DLL een eigen .def, zodat ook de interne bibliotheeknaam klopt.
      for naam in hasp_windows_x64 hasp_windows_x64_105293 hasp_windows_x64_106993; do
        sed "s/^LIBRARY .*/LIBRARY $naam/" src/hasp_windows.def > "out/$naam.def"
        x86_64-w64-mingw32-gcc -shared -O2 -Wall -Wextra -o "out/$naam.dll" \
          src/dongle.c src/hasp_preload_win.c "out/$naam.def"
      done
      sed "s/^LIBRARY .*/LIBRARY hasp_windows_32/" src/hasp_windows.def > out/hasp_windows_32.def
      i686-w64-mingw32-gcc -shared -O2 -Wall -Wextra -o out/hasp_windows_32.dll \
        src/dongle.c src/hasp_preload_win.c out/hasp_windows_32.def
      # Het meetprogramma voor de Windows-kant (tests/hasp_win_probe.c): laadt de DLL en roept de
      # ordinalen aan die Time Crisis 5 importeert.
      x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -o out/hasp_probe.exe /t/hasp_win_probe.c
      ls -l out/*.dll out/hasp_probe.exe
    '
    for naam in hasp_windows_x64.dll hasp_windows_x64_105293.dll hasp_windows_x64_106993.dll hasp_windows_32.dll; do
      [[ -f "$ROOT/hasp/out/$naam" ]] || { echo "let op: $naam is niet gebouwd" >&2; continue; }
      install -m 0644 "$ROOT/hasp/out/$naam" "$OUT/windows/$naam"
      BUILT+=("$naam")
    done
    [[ -f "$ROOT/hasp/out/hasp_probe.exe" ]] && install -m 0755 "$ROOT/hasp/out/hasp_probe.exe" "$OUT/windows/hasp_probe.exe"
    WIN_OK=1
  fi
fi

ls -l "$OUT"/ravengun-hasp*.so
[[ -d "$OUT/windows" ]] && ls -l "$OUT/windows" || true

# ------------------------------------------------------------------ 3. de zelftest
if [[ "$DO_TEST" == 1 ]]; then
  echo
  echo "zelftest (tegen de eerste dongle die we vinden):"
  $CC -O2 -Wall -Wextra -o "$OUT/.selftest" "$SRC/selftest.c" "$SRC/dongle.c"
  map=""
  for d in "$ROOT"/patches/*/; do
    [[ -f "$d/dongle.json" ]] && grep -q '"hasp' "$d/dongle.json" 2>/dev/null && map="$d" && break
  done
  [[ -n "$map" ]] || for d in "$OUT"/*/; do [[ -f "$d/dongle.json" ]] && map="$d" && break; done
  if [[ -n "$map" ]]; then
    "$OUT/.selftest" "${map%/}" || true
  else
    echo "  (nog geen dongle-map gevonden; zet er een in patches/<Game>/dongle.json)"
  fi
  rm -f "$OUT/.selftest"
fi

# ------------------------------------------------------------------ 4. de drop-ins installeren
if [[ "$DO_DROPIN" == 1 ]]; then
  echo
  echo "drop-ins installeren (docs/dropins.md):"
  gezet=0
  for ini in "$ROOT"/patches/*/dropin/dropin.ini; do
    [[ -f "$ini" ]] || continue
    game="$(basename "$(dirname "$(dirname "$ini")")")"
    [[ -n "$ONLY_GAME" && "$game" != "$ONLY_GAME" ]] && continue
    dir="$(dirname "$ini")"
    while IFS= read -r sectie; do
      naam="${sectie#[}"; naam="${naam%]}"
      [[ -n "$naam" ]] || continue
      bron=""
      for b in "${BUILT[@]}"; do [[ "$b" == "$naam" ]] && bron="$OUT/$naam"; done
      [[ -n "$bron" && -f "$bron" ]] || { [[ -f "$OUT/windows/$naam" ]] && bron="$OUT/windows/$naam"; }
      if [[ -n "$bron" && -f "$bron" ]]; then
        install -m 0755 "$bron" "$dir/$naam"
        echo "  $game/$naam"
        gezet=$((gezet + 1))
      elif [[ ! -f "$dir/$naam" ]]; then
        echo "  $game/$naam: niet gebouwd en niet aanwezig; de drop-in doet niets" >&2
      fi
    done < <(grep -E '^\s*\[' "$ini" | tr -d ' \r')
  done
  [[ "$gezet" == 0 ]] && echo "  (geen drop-in-secties gevonden die dit script kan bouwen)"
fi

echo
echo "klaar. De emulator staat in $OUT; de donglegegevens per game in patches/<Game>/dongle.json + dongle/."
echo "Controleer met: scripts/hasp.py --list en scripts/hasp.py --game <Game> --json"
