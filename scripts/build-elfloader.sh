#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Bouwt rg-elfloader: onze eigen i386-ELF-lader voor de ElfLdr2-familie (begin met GoGoStrike), zonder
# BudgieLoader en zonder Wine. Bron: tools/rg-elfloader/rg_elfloader.c. Zie patches/ELF-LADER-SPEC.md.
#
# De lader is i386 (hij deelt het adresbeeld met het 32-bits spel) en een gewoon glibc-programma dat
# libdl gebruikt. Resultaat: tools/rg-elfloader/rg-elfloader (en een kopie in ~/.local/share/ravengun/bin/).
#
# --alleen-bouwen <uit>: bouw met precies dezelfde vlaggen naar <uit> en installeer NIETS. Daarmee
# controleert tests/elfloader_install_test.sh of de gebouwde lader nog uit de huidige bron komt.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/tools/rg-elfloader/rg_elfloader.c"
OUT="$ROOT/tools/rg-elfloader/rg-elfloader"
CC="${CC:-gcc}"
INSTALLEER=1
if [[ "${1:-}" == "--alleen-bouwen" ]]; then
  [[ -n "${2:-}" ]] || { echo "gebruik: $0 [--alleen-bouwen <uit>]" >&2; exit 2; }
  OUT="$2"
  INSTALLEER=0
fi

[[ -f "$SRC" ]] || { echo "geen $SRC" >&2; exit 1; }

# De 32-bits bouw vraagt gnu/stubs-32.h. Die ontbreekt in lib32-glibc op SteamOS (dev-header, geen
# runtime-bestand). Maak hem zo nodig zelf aan, met dezelfde inhoud als stubs-64.h -- dezelfde verouderde
# BSD-stubs zijn op elke architectuur hetzelfde. Zo doet scripts/rt-glfix.sh het ook.
INC="$ROOT/tools/rg-elfloader/inc"
if [[ ! -f /usr/include/gnu/stubs-32.h ]]; then
  mkdir -p "$INC/gnu"
  cp -f /usr/include/gnu/stubs-64.h "$INC/gnu/stubs-32.h" 2>/dev/null || : > "$INC/gnu/stubs-32.h"
  EXTRA_INC="-I$INC"
else
  EXTRA_INC=""
fi

echo "bouwen: i386 -> rg-elfloader"
# -no-pie + vaste basis is niet nodig voor de lader zelf (hij mapt het spel op vaste adressen met MAP_FIXED);
# wel -m32. -rdynamic zodat dlsym(RTLD_DEFAULT,...) ook de lader-symbolen ziet indien nodig.
# -z execstack: de oude gastbibliotheken (liballeg.so.4.0, 2007) vragen een uitvoerbare stack via hun
# PT_GNU_STACK. Moderne glibc weigert dat bij dlopen ("cannot enable executable stack"). Door de lader
# zelf met een uitvoerbare stack te starten, heeft het proces er al een en hoeft dlopen niets te wijzigen.
# Zo hoeven we de bibliotheken niet aan te raken (ze staan in TP-runtime/NAS; daar schrijven we niet).
#
# GEEN -rdynamic: anders komen de symbolen van de lader zelf (o.a. `main`) in de dynamische symbooltabel
# en wint de lader-`main` bij het oplossen van het door-de-gast-geimporteerde `main` (Allegro levert `main`,
# de app levert `_mangled_main`). Gemeten: met -rdynamic draaide de lader zijn eigen main opnieuw. Zonder
# -rdynamic lost de gast-`main` op naar liballeg, zoals het hoort.
"$CC" -m32 -O2 -Wall -Wextra $EXTRA_INC -Wl,-z,execstack -o "$OUT" "$SRC" -ldl
file "$OUT"
[[ "$INSTALLEER" == 1 ]] || exit 0

DEST="$HOME/.local/share/ravengun/bin"
mkdir -p "$DEST"
cp -f "$OUT" "$DEST/rg-elfloader"

# Zichtbaar maken WELKE lader waar staat, zodat een benchrun nooit stil de oude geinstalleerde lader meet
# (dat gebeurde: de geinstalleerde kopie liep achter op de DT_GNU_HASH-reparatie). tests/elfloader_install_test.sh
# faalt als deze twee verschillen.
BUILT_SHA="$(sha256sum "$OUT" | cut -d' ' -f1)"
DEST_SHA="$(sha256sum "$DEST/rg-elfloader" | cut -d' ' -f1)"
echo "gebouwd:    $OUT"
echo "  sha256    $BUILT_SHA"
echo "geplaatst:  $DEST/rg-elfloader"
echo "  sha256    $DEST_SHA"
if [[ "$BUILT_SHA" != "$DEST_SHA" ]]; then
  echo "FOUT: de geplaatste lader verschilt van de gebouwde (installatie mislukt)" >&2
  exit 1
fi
echo "ok: gebouwde en geplaatste lader zijn gelijk"
