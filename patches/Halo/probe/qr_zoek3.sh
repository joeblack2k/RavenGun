#!/bin/bash
# DE SCHOONSTE ROUTE: kan de kast verteld worden dat hij GEEN scanners/geweer heeft?
# Lezen: de toestandsbestanden van de kast zelf, en de binary rond de toestandsnamen.
set -u
H="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo"
UP=~/.local/share/ravengun/native/Halo/0/upper
BIN="$H/game"

echo "############ 1. DE TOESTANDSBESTANDEN VAN DE KAST (aud/save/*.xml)"
for f in system io_connections last_connection online_ipaddrsports collection; do
  x="$UP/pmuser/aud/save/$f.0.xml"
  [ -f "$x" ] || continue
  printf '  === %s (%s bytes)\n' "$(basename "$x")" "$(stat -c%s "$x")"
  head -6 "$x" | sed 's/^/    /'
  echo "    --- regels met qr/webcam/gun/scanner/factory/camera/config:"
  grep -inE 'qr|webcam|gun|scanner|factory|camera|config|present|detect' "$x" 2>/dev/null | head -12 | sed 's/^/      /'
done

echo
echo "############ 2. DE INSTELLINGEN VAN DE KAST (adj/save: coin, sound, tickets)"
for d in coin sound tickets; do
  p="$UP/pmuser/adj/save/$d"
  [ -d "$p" ] || continue
  echo "  === adj/save/$d:"
  ls -l "$p" 2>/dev/null | head -6 | sed 's/^/    /'
  for f in "$p"/*; do [ -f "$f" ] && { printf '    --- %s (%s bytes)\n' "$(basename "$f")" "$(stat -c%s "$f")"; head -4 "$f" 2>/dev/null | sed 's/^/      /'; }; done
done

echo
echo "############ 3. EN HET CONFIGURATIEBESTAND VAN DE KAST ZELF"
for f in "$UP/pmuser/connection.conf" "$UP/pmuser/adj/save/connection.conf" "$UP/pmuser/coinaud"; do
  [ -e "$f" ] && { echo "  === $f"; ls -l "$f" | sed 's/^/    /'; head -10 "$f" 2>/dev/null | sed 's/^/    /'; }
done
find "$UP" -maxdepth 3 -iname '*conf*' -o -maxdepth 3 -iname '*.cfg' 2>/dev/null | head -8 | sed 's/^/  gevonden: /'

echo
echo "############ 4. DE BINARY ROND DE TOESTANDSNAMEN (wat beslist de toestand?)"
for T in "ATTRACT_GUN%i_DISABLED" "ATTRACT_WEBCAM_CONFIG" "P%u_WEBCAM" "AUTOID_DONE" "webcamOrderAudit"; do
  off=$(grep -abo "$T" "$BIN" 2>/dev/null | head -1 | cut -d: -f1)
  [ -z "${off:-}" ] && continue
  s=$(( off > 300 ? off - 300 : 0 ))
  echo "  === '$T' op 0x$off; de strings eromheen (dat zijn de buren, vaak de sleutels):"
  dd if="$BIN" bs=1 skip=$s count=700 2>/dev/null | strings -n 5 | head -22 | sed 's/^/    /'
done

echo
echo "############ 5. ZOEK DE SLEUTELS: hoe noemt de game 'aanwezig/afwezig' voor geweer en webcam?"
strings -n 4 "$BIN" 2>/dev/null | grep -iE '^(gun|webcam|scanner|qr|weapon)[A-Za-z0-9_]*$' | sort -u | head -25 | sed 's/^/  /'
echo "  --- en combinaties met present/enabled/installed/disabled/detected:"
strings -n 6 "$BIN" 2>/dev/null | grep -iE '(gun|webcam|scanner|qr).*(present|enable|disable|install|detect|absent|none)|((present|enable|disable|install|detect|absent).*(gun|webcam|scanner))' | sort -u | head -20 | sed 's/^/  /'
echo "== done"
