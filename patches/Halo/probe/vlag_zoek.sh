#!/bin/bash
# ZOEK DE VLAG: zegt de kast zelf ergens dat zijn inrichting niet compleet is? LEZEN, niets zetten.
set -u
UP=~/.local/share/ravengun/native/Halo/0/upper
H="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo"

echo "############ 1. ALLE AdjValStore-BESTANDEN (instellingen) MET HUN INHOUD"
for d in "$UP/pmuser/adj/save"/*/; do
  [ -d "$d" ] || continue
  echo "  === $(basename "$d")"
  for f in "$d"*; do
    [ -f "$f" ] || continue
    # de RTDT-bestanden hebben een leesbare staart met name/version/time_set/(value)
    txt=$(strings -n 3 "$f" 2>/dev/null | tr '\n' '|')
    printf '    %-26s %s\n' "$(basename "$f")" "$txt"
  done
done

echo
echo "############ 2. DE TAG-NAMEN IN DE XML-TOESTANDSBESTANDEN (wat houdt de kast bij?)"
for x in "$UP/pmuser/aud/save/"*.0.xml; do
  [ -f "$x" ] || continue
  echo "  === $(basename "$x")"
  # de bestanden zijn UTF-16-achtig (elke letter met een nul ertussen); strings haalt dat eruit
  strings -n 3 "$x" 2>/dev/null | grep -oE '<[A-Za-z_][A-Za-z0-9_\[\]\.]*>' | sort -u | head -24 | sed 's/^/    /'
done

echo
echo "############ 3. WAARDEN DIE OP "NIET AANWEZIG" KUNNEN WIJZEN"
for x in "$UP/pmuser/aud/save/"*.0.xml; do
  [ -f "$x" ] || continue
  hits=$(strings -n 3 "$x" 2>/dev/null | grep -iE 'webcam|gun|qr|scanner|factory|setup|contract|modem|order|present|detect' | head -8)
  [ -n "$hits" ] && { echo "  === $(basename "$x")"; echo "$hits" | sed 's/^/    /'; }
done

echo
echo "############ 4. DE SLEUTELWOORDEN IN ALLES WAT DE KAST SCHRIJFT"
for w in factory setup provision complete incomplete webcam gun qr scanner contract modem wifi online order present detect; do
  n=$(grep -rlia "$w" "$UP/pmuser" 2>/dev/null | wc -l)
  printf '  %-12s in %s bestand(en)\n' "$w" "$n"
done
echo "  --- en WELKE bestanden dat zijn (zonder de mesacache):"
grep -rlia -E 'factory|webcam|scanner|contract' "$UP/pmuser" 2>/dev/null | sed 's/^/    /' | head -12

echo
echo "############ 5. DE CONFIGURATIEBESTANDEN VAN DE KAST"
for f in "$UP/pmuser/connection.conf" "$UP/pmuser/coinaud" "$UP/pmuser/online_audits"; do
  [ -e "$f" ] && { echo "  === $f"; ls -ld "$f" | sed 's/^/    /'; strings -n 4 "$f" 2>/dev/null | head -10 | sed 's/^/    /'; }
done
find "$UP/pmuser" -maxdepth 2 -iname '*conf*' -o -maxdepth 2 -iname '*.cfg' -o -maxdepth 2 -iname '*_conf*' 2>/dev/null | head -8 | sed 's/^/  gevonden: /'

echo
echo "############ 6. DE SLEUTELWOORDEN IN DE SPELBINARY ROND DE VLAGGEN"
BIN="$H/game"
strings -n 5 "$BIN" 2>/dev/null | grep -iE '^(factory|setup|provision|complete|webcam|gun|qr|scanner)[A-Za-z0-9_]*$' | sort -u | head -30 | sed 's/^/  /'
echo "  --- en namen van instellingen die de kast bewaart (AdjVal-patroon):"
strings -n 5 "$BIN" 2>/dev/null | grep -iE 'factorySetup|setupComplete|provision|webcamOrder|gunSerial|numAttached|gunInfo|scannerConfig' | sort -u | head -20 | sed 's/^/  /'
echo "== done"
