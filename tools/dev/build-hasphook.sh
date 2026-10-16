#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Bouwt de HASP-inline-hook van RavenGun en legt hem in de drop-in-map van elke game die hem nodig
# heeft.
#
# De bron staat in de repo (tools/rt-hasphook/hasp_inline_hook.c). Het resultaat hoort per game in
# de map van die game, want dat is de afspraak (docs/dropins.md):
#
#   patches/<Game>/dropin/libravengun-hasphook.so    de 32-bits bibliotheek
#   patches/<Game>/dropin/libravengun-hasphook-x64.so de 64-bits bibliotheek
#   patches/<Game>/dropin/dropin.ini                 waar hij heen gaat en welke variabelen erbij horen
#
# WAAROM EEN APARTE BIBLIOTHEEK EN GEEN LD_PRELOAD VAN DE EMULATOR
# ---------------------------------------------------------------
# Gemeten (docs/hasp-elf-hook.md): de HASP-cliënt van deze games zit STATISCH in het `game`-bestand.
# Er is geen PLT-slot en geen GOT-invoer voor hasp, dus `LD_PRELOAD` van de emulator zet hem wel in
# het proces maar de aanroepen gaan er niet langs. Deze bibliotheek haakt de functies op hun
# ingangspunt aan en leidt ze naar de emulator.
#
# Gebruik:
#   tools/dev/build-hasphook.sh                  bouwen en naar alle games in GAMES leggen
#   tools/dev/build-hasphook.sh --game <Naam>    alleen die game (herhaalbaar)
#   tools/dev/build-hasphook.sh --no-place       alleen bouwen, niets wegschrijven
#   tools/dev/build-hasphook.sh --check          alleen kijken of de geplaatste kopieën actueel zijn
#   tools/dev/build-hasphook.sh --selftest       ook de zelftest draaien
#
# Let op: de 32-bits bouw vraagt de dev-header gnu/stubs-32.h. Die ontbreekt in lib32-glibc op
# SteamOS (het is een dev-header, geen runtime-bestand). Dit script gebruikt de kopie die
# scripts/build-hasp.sh al aanmaakt in hasp/src/inc/gnu/, en maakt hem zo nodig zelf aan.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="$ROOT/tools/rt-hasphook/hasp_inline_hook.c"
CC="${CC:-gcc}"
LIB32="libravengun-hasphook.so"
LIB64="libravengun-hasphook-x64.so"

# De games waarvan gemeten is dat hun HASP-cliënt statisch gelinkt is (docs/hasp-elf-hook.md).
GAMES=(JurassicPark GalagaAssault Halo Terminator)

DO_PLACE=1
DO_CHECK=0
DO_SELFTEST=0
KEUS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-place) DO_PLACE=0 ;;
    --check)    DO_CHECK=1 ;;
    --selftest) DO_SELFTEST=1 ;;
    --game)     shift; KEUS+=("${1:-}") ;;
    -h|--help)  sed -n '2,32p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
  shift
done
if [[ ${#KEUS[@]} -gt 0 ]]; then
  GAMES=("${KEUS[@]}")
fi

[[ -f "$SRC" ]] || { echo "geen $SRC" >&2; exit 1; }

# De 32-bits dev-header, alleen als hij ontbreekt.
INC32=()
if [[ ! -e /usr/include/gnu/stubs-32.h ]]; then
  mkdir -p "$ROOT/hasp/src/inc/gnu"
  if [[ ! -e "$ROOT/hasp/src/inc/gnu/stubs-32.h" ]]; then
    {
      echo "/* Lokaal gemaakt door tools/dev/build-hasphook.sh: gnu/stubs-32.h ontbreekt in lib32-glibc."
      echo "   De inhoud is gelijk aan gnu/stubs-64.h; zie /usr/include/gnu/stubs.h voor de selectie. */"
      echo "#ifdef _LIBC"
      echo " #error Applications may not define the macro _LIBC"
      echo "#endif"
      grep '^#define __stub_' /usr/include/gnu/stubs-64.h || true
    } > "$ROOT/hasp/src/inc/gnu/stubs-32.h"
    echo "gemaakt: hasp/src/inc/gnu/stubs-32.h (lokaal; de dev-header ontbrak)"
  fi
  INC32=(-I"$ROOT/hasp/src/inc")
fi

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

CFLAGS=(-shared -fPIC -O2 -Wall -Wextra -Wno-format-truncation)
echo "bouwen: 32-bits -> $LIB32"
$CC -m32 "${INC32[@]}" "${CFLAGS[@]}" -o "$OUT/$LIB32" "$SRC" -ldl -lpthread
echo "bouwen: 64-bits -> $LIB64"
$CC "${CFLAGS[@]}" -o "$OUT/$LIB64" "$SRC" -ldl -lpthread
file "$OUT/$LIB32" "$OUT/$LIB64"
ls -l "$OUT/$LIB32" "$OUT/$LIB64"

# De twee symbolen die glibc van een LD_AUDIT-module verwacht, moeten er zijn.
for l in "$LIB32" "$LIB64"; do
  if ! nm -D --defined-only "$OUT/$l" | grep -q ' la_version$'; then
    echo "FOUT: la_version ontbreekt in $l; dan laadt glibc de module niet als LD_AUDIT-module" >&2
    exit 1
  fi
done
echo "controle: la_version en la_preinit aanwezig in beide breedtes"

if [[ "$DO_SELFTEST" == 1 ]]; then
  echo
  echo "zelftest (32-bits gastheer die zijn eigen hasp-functies statisch in het beeld heeft):"
  $CC -m32 "${INC32[@]}" -O1 -rdynamic -o "$OUT/hasp_zelftest" \
    "$ROOT/tools/rt-hasphook/hasp_zelftest.c"
  # Een game met een volledige dongle.json, zodat de emulator echte gegevens teruggeeft. Zonder
  # dongle antwoordt hij met HASP_STATUS_INVALID_PARAMETER (503) en dat is geen fout van de hook.
  DONGGAME=""
  for g in "${GAMES[@]}"; do
    if [[ -f "$ROOT/patches/$g/dongle.json" ]]; then DONGGAME="$g"; break; fi
  done
  if [[ -z "$DONGGAME" ]]; then
    for g in Halo JurassicPark GalagaAssault Terminator; do
      if [[ -f "$ROOT/patches/$g/dongle.json" ]]; then DONGGAME="$g"; break; fi
    done
  fi
  echo "  zonder hook (de statisch gelinkte cliënt hoort te antwoorden):"
  ( cd "$OUT" && HASP_ZELFTEST_WACHT=0 ./hasp_zelftest 2>&1 | tail -3 | sed 's/^/    /' ) || true
  echo "  met hook${DONGGAME:+ + de dongle van $DONGGAME}:"
  ( cd "$OUT" && LD_PRELOAD="$OUT/$LIB32" RAVENGUN_HASP_VERBOSE=1 \
      RAVENGUN_HASP_GAME="${DONGGAME:-zelftest}" RAVENGUN_HASP_DIR="$ROOT/patches" \
      RAVENGUN_HASP_ELF_WACHT=10 \
      ./hasp_zelftest 2>&1 | grep -E 'hasp_login ->|grootte|eerste bytes|echte_aanroepen|RESULTAAT' \
      | sed 's/^/    /' ) || true
fi

if [[ "$DO_CHECK" == 1 ]]; then
  rc=0
  for g in "${GAMES[@]}"; do
    for l in "$LIB32" "$LIB64"; do
      d="$ROOT/patches/$g/dropin/$l"
      if [[ ! -f "$d" ]]; then
        echo "ONTBREEKT  $d"
        rc=1
      elif cmp -s "$OUT/$l" "$d"; then
        echo "actueel    $d"
      else
        echo "AFWIJKEND  $d (opnieuw bouwen en plaatsen)"
        rc=1
      fi
    done
  done
  exit $rc
fi

if [[ "$DO_PLACE" == 0 ]]; then
  echo
  echo "klaar (niet geplaatst). De gebouwde bestanden stonden in $OUT."
  exit 0
fi

for g in "${GAMES[@]}"; do
  d="$ROOT/patches/$g/dropin"
  [[ -d "$ROOT/patches/$g" ]] || { echo "LET OP: patches/$g bestaat niet; overgeslagen" >&2; continue; }
  mkdir -p "$d"
  for l in "$LIB32" "$LIB64"; do
    if [[ -f "$d/$l" ]] && cmp -s "$OUT/$l" "$d/$l"; then
      echo "gelijk     patches/$g/dropin/$l"
    else
      install -m 0644 "$OUT/$l" "$d/$l"
      echo "geplaatst  patches/$g/dropin/$l"
    fi
  done
  if [[ ! -f "$d/dropin.ini" ]]; then
    echo "LET OP: patches/$g/dropin/dropin.ini ontbreekt; de launcher ziet de drop-in dan niet" >&2
  fi
done

echo
echo "klaar."
