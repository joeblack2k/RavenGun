#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Bouwt de cdecl-brug van RavenGun voor de 32-bits Raw Thrills-ELF's (rt-glfix) en legt hem in de
# drop-in-map van elke game die hem nodig heeft.
#
# De bron staat in de repo (tools/rt-glfix/rt_glfix.c). Het resultaat hoort per game in de map van
# die game, want dat is de afspraak (docs/dropins.md):
#
#   patches/<Game>/dropin/librt-glfix.so     de bibliotheek zelf
#   patches/<Game>/dropin/dropin.ini         waar hij heen gaat en welke omgevingsvariabelen erbij horen
#
# Dezelfde bibliotheek werkt voor de hele familie: hij zoekt de GOT-invoeren van de game tijdens
# het draaien op en raakt alleen invoeren aan die naar een `gl*`-export van opengl32.dll wijzen.
# Er staat dus drie keer hetzelfde bestand in de repo; een wijziging begint altijd hier, in
# tools/rt-glfix/, en dit script verdeelt hem. Zie docs/rt-glfix.md.
#
# Gebruik:
#   scripts/rt-glfix.sh                 bouwen en naar alle games in GAMES leggen
#   scripts/rt-glfix.sh --game <Naam>   alleen die game (herhaalbaar)
#   scripts/rt-glfix.sh --no-place      alleen bouwen, niets wegschrijven
#   scripts/rt-glfix.sh --check         alleen kijken of de geplaatste kopieën actueel zijn
#
# Let op: de 32-bits bouw vraagt de dev-header gnu/stubs-32.h. Die ontbreekt in lib32-glibc op
# SteamOS (het is een dev-header, geen runtime-bestand). Dit script maakt hem zo nodig zelf aan in
# tools/rt-glfix/inc/gnu/, met dezelfde inhoud als gnu/stubs-64.h -- dezelfde verouderde
# BSD-functies zijn op elke architectuur een stub. Dat is genoeg om te compileren en te linken
# tegen de 32-bits libc die er wel is (/usr/lib32/libc.so.6). Zo doet scripts/build-hasp.sh het ook.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/tools/rt-glfix/rt_glfix.c"
CC="${CC:-gcc}"
LIB="librt-glfix.so"

# De games die deze brug nodig hebben: de 32-bits Raw Thrills-ELF's zonder sectiekoppen, waarvan
# ElfLdr2 de symbolen niet kan parsen en de GL-sloten dus niet van cdecl-adapters voorziet.
GAMES=(JurassicPark PacmanChomp GalagaAssault)

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
    -h|--help)  sed -n '2,30p' "$0"; exit 0 ;;
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
  mkdir -p "$ROOT/tools/rt-glfix/inc/gnu"
  if [[ ! -e "$ROOT/tools/rt-glfix/inc/gnu/stubs-32.h" ]]; then
    {
      echo "/* Lokaal gemaakt door scripts/rt-glfix.sh: gnu/stubs-32.h ontbreekt in lib32-glibc."
      echo "   De inhoud is gelijk aan gnu/stubs-64.h; zie /usr/include/gnu/stubs.h voor de selectie. */"
      echo "#ifdef _LIBC"
      echo " #error Applications may not define the macro _LIBC"
      echo "#endif"
      grep '^#define __stub_' /usr/include/gnu/stubs-64.h || true
    } > "$ROOT/tools/rt-glfix/inc/gnu/stubs-32.h"
    echo "gemaakt: tools/rt-glfix/inc/gnu/stubs-32.h (lokaal; de dev-header ontbrak)"
  fi
  INC32=(-I"$ROOT/tools/rt-glfix/inc")
fi

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

CFLAGS=(-m32 -shared -fPIC -O2 -Wall -Wextra -Wno-unused-parameter)
echo "bouwen: 32-bits -> $LIB"
$CC "${INC32[@]}" "${CFLAGS[@]}" -o "$OUT/$LIB" "$SRC"
file "$OUT/$LIB"
ls -l "$OUT/$LIB"

# De twee symbolen die glibc van een LD_AUDIT-module verwacht, moeten er zijn.
if ! nm -D --defined-only "$OUT/$LIB" | grep -q ' la_version$'; then
  echo "FOUT: la_version ontbreekt; dan laadt glibc de module niet als LD_AUDIT-module" >&2
  exit 1
fi
echo "controle: la_version en la_preinit aanwezig"

# De zelftest: een 32-bits gastheer laadt de bibliotheek zodat de constructor de cdecl-brug
# bouwt en test (een neppe stdcall-functie met `ret 8` aanroepen en kijken of de stapelpointer
# daarna precies gelijk is). Dit vangt fouten in de brug zonder dat er een game aan te pas komt.
if [[ "$DO_SELFTEST" == 1 ]]; then
  $CC "${INC32[@]}" -m32 -O2 -o "$OUT/gastheer32" "$ROOT/tools/rt-glfix/gastheer32.c" -ldl
  echo
  echo "zelftest (32-bits gastheer):"
  RAVENGUN_GLFIX_GAME=zelftest RAVENGUN_GLFIX_LOG=/dev/stderr RAVENGUN_GLFIX_SECS=1 \
    "$OUT/gastheer32" "$OUT/$LIB" 2>&1 | sed 's/^/  /'
fi

if [[ "$DO_CHECK" == 1 ]]; then
  rc=0
  for g in "${GAMES[@]}"; do
    d="$ROOT/patches/$g/dropin/$LIB"
    if [[ ! -f "$d" ]]; then
      echo "ONTBREEKT  $d"
      rc=1
    elif cmp -s "$OUT/$LIB" "$d"; then
      echo "actueel    $d"
    else
      echo "AFWIJKEND  $d (opnieuw bouwen en plaatsen)"
      rc=1
    fi
  done
  exit $rc
fi

if [[ "$DO_PLACE" == 0 ]]; then
  echo
  echo "klaar (niet geplaatst). Het gebouwde bestand stond in $OUT."
  exit 0
fi

for g in "${GAMES[@]}"; do
  d="$ROOT/patches/$g/dropin"
  [[ -d "$ROOT/patches/$g" ]] || { echo "LET OP: patches/$g bestaat niet; overgeslagen" >&2; continue; }
  mkdir -p "$d"
  if [[ -f "$d/$LIB" ]] && cmp -s "$OUT/$LIB" "$d/$LIB"; then
    echo "gelijk     patches/$g/dropin/$LIB"
  else
    install -m 0644 "$OUT/$LIB" "$d/$LIB"
    echo "geplaatst  patches/$g/dropin/$LIB"
  fi
  if [[ ! -f "$d/dropin.ini" ]]; then
    echo "LET OP: patches/$g/dropin/dropin.ini ontbreekt; de launcher ziet de drop-in dan niet" >&2
  fi
done

echo
echo "klaar. Testen met (Jurassic Park):"
echo "  cd ~/projects/ravengun"
echo "  XAUTHORITY=/run/user/1000/xauth_NBglcp DISPLAY=:0 \\"
echo "    python3 scripts/launch-direct.py JurassicPark --run \\"
echo "    --env LD_AUDIT=\$HOME/.local/share/ravengun/runtime/views/JurassicPark/$LIB"
echo "Het logboek komt in de view te staan (RAVENGUN_GLFIX_LOG=rt-glfix.log)."
echo "Zonder de drop-in (ter vergelijking) laat je --env weg."
