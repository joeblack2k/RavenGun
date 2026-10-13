#!/bin/bash
# Bewijs ZONDER de bench dat de generieke PE-inline-hook werkt, op een ECHT PE-geheugenbeeld van
# Luigi's Mansion (image-gesloten.bin: offset == RVA bij ImageBase 0x140000000).
#
#   P1  positief: de 17 ECHTE adressen met een synthetische prologue -> 17 van 17 aangehaakt
#   P2  weigering: de ONGEWIJZIGDE dump (de gesloten kern heeft er al e9 gezet) -> 17 afgekeurd
#   P3  weigering: een adres in .rdata, een adres buiten het beeld, en een verkeerde versie
#   P4  de stub DRAAIT: een aangehaakte functie geeft 0 terug
#   P5  het GEMETEN antwoord: amDongleGetNetworkAddress schrijft 192.168.1.0 in *(uint32*)rcx
set -u
P=~/projects/ravengun-patches/patches/LuigisMansion
H=~/projects/ravengun-patches/patches/Halo/probe
BEELD="${BEELD:-/tmp/b0-geheugen-b/image-gesloten.bin}"
LIJST="$P/dropin/dongle-haken.ini"
TMP=$(mktemp -d)
ADR="0x2b3b70 0x2b3d30 0x2b3ff0 0x2b40b0 0x2b4170 0x2b4270 0x2b4320 0x2b4740 0x2b50f0 0x2b5180 0x2b5200 0x2b55a0 0x2b56f0 0x2b5860 0x2b5cb0 0x2b6050 0x2b6160"

echo "############ 0. bouwen"
[ -f "$BEELD" ] || { echo "  beeld ontbreekt: $BEELD"; exit 1; }
[ -f "$LIJST" ] || { echo "  lijst ontbreekt: $LIJST"; exit 1; }
echo "  beeld: $BEELD  ($(stat -c%s "$BEELD") B, sha256 $(sha256sum "$BEELD" | cut -c1-16))"
echo "  lijst: $LIJST  ($(grep -c '^\[am' "$LIJST") regels)"
gcc -Wall -Wextra -O2 -I"$H" -o "$TMP/toets" "$H/hook-pe-toets.c" "/home/deck/projects/ravengun/tools/rt-hasphook/rg_pe_hook.c" 2>&1 | head -20
echo "  gcc exitcode ${PIPESTATUS[0]}"
[ -x "$TMP/toets" ] || { echo "  bouwen mislukt"; exit 1; }

echo
echo "############ P1. positief: de ECHTE 17 adressen, met een synthetische prologue"
# De originele prologues zijn door de gesloten kern overschreven (e9), en onder ONZE kern is .text
# op elk bemonsterd moment nog nul -- de originele bytes zijn dus niet meetbaar. Voor deze proef
# zetten we op die 17 adressen een normale prologue. De adressen en de PE-structuur zijn echt.
python3 - "$BEELD" "$TMP/beeld-synthetisch.bin" $ADR <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
adressen = [int(a, 16) for a in sys.argv[3:]]
d = bytearray(open(src, "rb").read())
for a in adressen:
    d[a:a+6] = b"\x40\x53\x48\x83\xec\x28"
open(dst, "wb").write(d)
print("  %d synthetische prologues geschreven" % len(adressen))
PY
"$TMP/toets" "$TMP/beeld-synthetisch.bin" "$LIJST" --verwacht 17 2>&1 | tail -4
echo "  exitcode ${PIPESTATUS[0]}"

echo
echo "############ P2. weigering: de ONGEWIJZIGDE dump (de gesloten kern zit er al in)"
"$TMP/toets" "$BEELD" "$LIJST" --verwacht 0 2>&1 | grep -E 'AFGEWEZEN: eerste|totaal aangehaakt|GESLAAGD' | head -4
echo "  exitcode ${PIPESTATUS[0]}"

echo
echo "############ P3. weigering: ongeldige adressen en een verkeerde versie"
{
  echo "beeld   = pe"; echo "basis   = beeld"; echo "grootte = 0x125e000"; echo "ingang  = 0xf23125"; echo
  echo "[in-rdata-niet-uitvoerbaar]"; echo "rva      = 0x357000"; echo "antwoord = ret0"; echo
  echo "[buiten-het-beeld]";            echo "rva      = 0x9000000"; echo "antwoord = ret0"
} > "$TMP/fout.ini"
"$TMP/toets" "$BEELD" "$TMP/fout.ini" 2>&1 | grep -E 'AFGEWEZEN|totaal' | sed 's/^/  /'
{
  echo "beeld   = pe"; echo "basis   = beeld"; echo "grootte = 0x999"; echo
  echo "[amDongleInit]"; echo "rva      = 0x2b4170"; echo "antwoord = ret0"
} > "$TMP/versie.ini"
"$TMP/toets" "$BEELD" "$TMP/versie.ini" 2>&1 | grep -E 'AFGEKEURD|totaal' | sed 's/^/  /'

echo
echo "############ P4. de stub DRAAIT: amDongleInit (RVA 0x2b4170) geeft 0 terug"
"$TMP/toets" "$TMP/beeld-synthetisch.bin" "$LIJST" --verwacht 17 --bel 0x2b4170 2>&1 | tail -5 | sed 's/^/  /'

echo
echo "############ P5. het GEMETEN antwoord: amDongleGetNetworkAddress (RVA 0x2b40b0)"
"$TMP/toets" "$TMP/beeld-synthetisch.bin" "$LIJST" --verwacht 17 --bel-net 0x2b40b0 2>&1 | tail -6 | sed 's/^/  /'
echo "  exitcode ${PIPESTATUS[0]}"

echo
echo "############ P6. de TODO-vorm bestaat nog (met een synthetische lijst)"
{
  echo "beeld   = pe"; echo "basis   = beeld"; echo "grootte = 0x125e000"; echo
  echo "[amDongleGetGameId]"; echo "rva      = 0x2b3d30"; echo "antwoord = TODO"
} > "$TMP/todo.ini"
"$TMP/toets" "$TMP/beeld-synthetisch.bin" "$TMP/todo.ini" --bel 0x2b3d30 2>&1 | tail -4 | sed 's/^/  /'
echo "== done ($TMP)"
