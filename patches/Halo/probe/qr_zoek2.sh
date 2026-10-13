#!/bin/bash
# Deel 2: de OPERATOR-DATA van de game, het configuratiebestand, en de QR-teksten in de .g7-archieven.
set -u
H="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo"
UP=~/.local/share/ravengun/native/Halo/0/upper

echo "############ 1. DE OPERATOR-DATA van de game (adj = adjustments, aud = audits)"
for d in "$UP/pmuser/adj/save" "$UP/pmuser/aud/save"; do
  echo "  === $d"
  ls -la "$d" 2>/dev/null | head -12 | sed 's/^/    /'
  for f in "$d"/*; do
    [ -f "$f" ] || continue
    printf '    --- %s (%s bytes)\n' "$(basename "$f")" "$(stat -c%s "$f")"
    strings -n 4 "$f" 2>/dev/null | head -12 | sed 's/^/      /'
  done
done

echo
echo "############ 2. de admin/configuratiebestanden van de kast zelf"
for f in "$H/utils/profile-sets-default.conf"; do
  [ -f "$f" ] || continue
  echo "  === $(basename "$f") ($(stat -c%s "$f") bytes)"
  grep -inE 'qr|scanner|factory|setup|install|cabinet|webcam|camera' "$f" 2>/dev/null | head -20 | sed 's/^/    /'
  echo "  --- en de eerste 12 regels:"
  head -12 "$f" | sed 's/^/    /'
done

echo
echo "############ 3. de QR-TEKSTEN in de .g7-archieven van de game"
find "$H/data" -maxdepth 2 -type f 2>/dev/null | head -20 | sed 's/^/  /'
echo "  --- zoeken naar de teksten in alle databestanden (kan even duren):"
for T in "QR Scanner" "not detected" "need to be configured" "Factory Setup"; do
  hits=$(grep -rlabo "$T" "$H/data" 2>/dev/null | head -3)
  printf '  %-24s %s\n' "$T" "${hits:-niet gevonden}"
done

echo
echo "############ 4. de omgeving van de toestandsnamen in de spelbinary"
BIN="$H/game"
for T in "ATTRACT_FACTORY_SETUP_INCOMP" "ActivateCell_FactorySetup_INST" "webcamID"; do
  off=$(grep -abo "$T" "$BIN" 2>/dev/null | head -1 | cut -d: -f1)
  [ -z "${off:-}" ] && { echo "  '$T' niet gevonden"; continue; }
  s=$(( off > 200 ? off - 200 : 0 ))
  echo "  === '$T' op 0x$off; strings eromheen:"
  dd if="$BIN" bs=1 skip=$s count=520 2>/dev/null | strings -n 5 | head -18 | sed 's/^/    /'
done

echo
echo "############ 5. welke PADEN gebruikt de game voor zijn instellingen?"
strings -n 6 "$BIN" 2>/dev/null | grep -E '^(/|%s)?[A-Za-z0-9_/%.]*(adj|aud|save|conf|setting|config|setup|factory|scanner|webcam|qr)[A-Za-z0-9_/%.]*$' \
  | sort -u | head -30 | sed 's/^/  /'
echo "== done"
