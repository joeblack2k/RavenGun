#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Bouwt rg-elfloader: onze eigen i386-ELF-lader voor de ElfLdr2-familie (begin met GoGoStrike), zonder
# BudgieLoader en zonder Wine. Bron: tools/rg-elfloader/rg_elfloader.c. Zie patches/ELF-LADER-SPEC.md.
#
# De lader is i386 (hij deelt het adresbeeld met het 32-bits spel) en een gewoon glibc-programma dat
# libdl gebruikt. Resultaat: tools/rg-elfloader/rg-elfloader (en een kopie in ~/.local/share/ravengun/bin/).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/tools/rg-elfloader/rg_elfloader.c"
OUT="$ROOT/tools/rg-elfloader/rg-elfloader"
CC="${CC:-gcc}"

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
"$CC" -m32 -O2 -Wall -Wextra -rdynamic $EXTRA_INC -Wl,-z,execstack -o "$OUT" "$SRC" -ldl
file "$OUT"

DEST="$HOME/.local/share/ravengun/bin"
mkdir -p "$DEST"
cp -f "$OUT" "$DEST/rg-elfloader"
echo "geplaatst: $DEST/rg-elfloader"
