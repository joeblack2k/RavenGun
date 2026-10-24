#!/bin/bash
# Bewijs van de WACHTROUTE: de hook zet de prologues om op het moment dat de pakker ze schrijft,
# niet bij het laden. Gesimuleerd met een ontpakker die na N ms de prologues wegschrijft.
set -u
P=~/projects/ravengun-patches/patches/LuigisMansion
H=~/projects/ravengun-patches/patches/Halo/probe
BEELD="${BEELD:-/tmp/b0-geheugen-b/image-gesloten.bin}"
TMP=$(mktemp -d)

echo "############ 0. bouwen"
gcc -Wall -Wextra -O2 -I"$H" -o "$TMP/toets" "$H/hook-pe-toets.c" "/home/deck/projects/ravengun/tools/rt-hasphook/rg_pe_hook.c" 2>&1 | head -20
echo "  gcc exitcode ${PIPESTATUS[0]}"
[ -x "$TMP/toets" ] || { echo "  bouwen mislukt"; exit 1; }

echo
echo "############ 1. het beeld klaarzetten: de 17 prologues op NUL (zoals vóór het uitpakken)"
RVAS=$(grep -E '^rva' "$P/dropin/dongle-haken.ini" | awk '{print $3}' | paste -sd, -)
echo "  17 RVA's: $RVAS"
python3 - "$BEELD" "$TMP/beeld-nul.bin" "$RVAS" <<'PY'
import sys
src, dst, lijst = sys.argv[1], sys.argv[2], sys.argv[3]
rvas = [int(x, 16) for x in lijst.split(",")]
d = bytearray(open(src, "rb").read())
for a in rvas:
    d[a:a+16] = b"\x00" * 16
open(dst, "wb").write(d)
print("  %d prologues op nul gezet -> %s" % (len(rvas), dst))
PY

echo
echo "############ 2. de lijst met de wachtroute (wacht_s = 5)"
sed "s|^game        = LuigisMansion|game        = LuigisMansion\nwacht_s     = 5|" \
    "$P/dropin/dongle-haken.ini" > "$TMP/lijst-wacht.ini"
grep -E '^wacht' "$TMP/lijst-wacht.ini" | sed 's/^/  /'

echo
echo "############ 3. de proef: ontpakker schrijft de prologues na 600 ms"
"$TMP/toets" "$TMP/beeld-nul.bin" "$TMP/lijst-wacht.ini" --verwacht 17 \
    --ontpak-na 600 --ontpak-rvas "$RVAS" 2>&1 \
  | grep -E 'ontpakker|wachtroute|totaal aangehaakt|GESLAAGD|MISLUKT|NOG NIET' | head -12 | sed 's/^/  /'
echo "  exitcode ${PIPESTATUS[0]}"

echo
echo "############ 4. en de controle: ZONDER wachtroute zet hij niets om (de pagina is nog nul)"
sed 's/^wacht_s.*/wacht_s     = 0/' "$TMP/lijst-wacht.ini" > "$TMP/lijst-nietwacht.ini"
"$TMP/toets" "$TMP/beeld-nul.bin" "$TMP/lijst-nietwacht.ini" --verwacht 0 \
    --ontpak-na 600 --ontpak-rvas "$RVAS" 2>&1 \
  | grep -E 'NOG NIET UITGEPAKT|totaal aangehaakt|GESLAAGD|MISLUKT' | head -4 | sed 's/^/  /'
echo "== done ($TMP)"
