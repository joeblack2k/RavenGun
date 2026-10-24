#!/bin/bash
# ts/43-mapping-toets.sh - STATISCH BEWIJS, GEEN GAME. Bouwt het testprogramma, mapt het echte
# gastbeeld, en laat de generieke HASP-hook los met patches/Terminator/hasp-haken.ini.
#
# Twee runs:
#   A. RAVENGUN_HASP_ELF_UIT=1  -> de hook keurt de lijst en patcht NIETS (veiligste bewijs)
#   B. zonder UIT               -> de hook haakt ook echt aan, in ONZE proceseigen beeldkopie
set -u
D="$HOME/projects/ravengun-patches/patches/Terminator"
HOOK="$HOME/projects/ravengun-patches/patches/Halo/dropin/libravengun-hasphook.so"
GAME="/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation /game"
INI="$D/hasp-haken.ini"
W=/tmp/ts-lijst
mkdir -p "$W"

echo "=== 0. vooraf ==="
ls -l "$HOOK" "$INI" 2>&1
file "$HOOK" | cut -c1-90
echo "  emulator (voor run B):"
ls -l "$HOME/.local/share/ravengun/hasp/ravengun-hasp.so" 2>&1
echo

echo "=== 1. testprogramma bouwen (32-bits, PIE) ==="
# -I is nodig: de 32-bits dev-header gnu/stubs-32.h ontbreekt op SteamOS; de lader-repo maakt hem
# lokaal aan in tools/rg-elfloader/inc. Zelfde truc als build-hasphook.sh gebruikt.
INC="$HOME/projects/ravengun/tools/rg-elfloader/inc"
ls -l "$INC/gnu/stubs-32.h" 2>&1
gcc -m32 -pie -fPIE -O1 -Wall -I"$INC" -o "$W/hasplijst-toets" "$D/hasplijst-toets.c" -ldl 2>&1 | head -20
ls -l "$W/hasplijst-toets" 2>&1
file "$W/hasplijst-toets" 2>&1 | cut -c1-80
echo

echo "=== 2. RUN A: alleen keuren (RAVENGUN_HASP_ELF_UIT=1), niets aangehaakt ==="
rm -f "$W/keuren.log"
RAVENGUN_HASP_ELF_GAME="$GAME" \
RAVENGUN_HASP_ELF_ADRESLIJST="$INI" \
RAVENGUN_HASP_ELF_LOG="$W/keuren.log" \
RAVENGUN_HASP_ELF_UIT=1 \
RAVENGUN_HASP_VERBOSE=1 \
"$W/hasplijst-toets" "$GAME" "$HOOK"
echo "--- exit=$? ---"
echo "--- log ---"
cat "$W/keuren.log" 2>&1
echo

echo "=== 3. RUN B: echt aanhaken (in onze eigen beeldkopie) ==="
rm -f "$W/haken.log"
RAVENGUN_HASP_ELF_GAME="$GAME" \
RAVENGUN_HASP_ELF_ADRESLIJST="$INI" \
RAVENGUN_HASP_ELF_LOG="$W/haken.log" \
RAVENGUN_HASP_VERBOSE=1 \
"$W/hasplijst-toets" "$GAME" "$HOOK"
echo "--- exit=$? ---"
echo "--- log ---"
cat "$W/haken.log" 2>&1
echo

echo "=== 4. samenvatting uit de logboeken ==="
echo "RUN A (keuren):"
grep -cE 'GELDIG' "$W/keuren.log" 2>/dev/null | sed 's/^/  GELDIG-regels: /'
grep -cE 'AFGEWEZEN' "$W/keuren.log" 2>/dev/null | sed 's/^/  AFGEWEZEN-regels: /'
grep -E 'AFGEWEZEN' "$W/keuren.log" 2>/dev/null | sed 's/^/  /'
echo "RUN B (haken):"
grep -E 'totaal aangehaakt|niet aangehaakt' "$W/haken.log" 2>/dev/null | sed 's/^/  /'
