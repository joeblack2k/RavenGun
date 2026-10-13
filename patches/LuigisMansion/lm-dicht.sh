#!/usr/bin/env bash
# lm-dicht.sh -- dichte bemonstering van het venster waarin ONZE kern sterft (en hetzelfde
# venster op de GESLOTEN kern als ijkpunt). Per moment: het percentage nul in .text en de
# 5 bytes op 0x1402b4170.
#
# GEBRUIK: bash lm-dicht.sh            # beide kernen
#          bash lm-dicht.sh onze       # alleen onze kern (zet de kern in en terug)
set -u
HIER="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WIE="${1:-beide}"
VENSTER="$(seq -s, 4.0 0.1 6.5)"
RUIMER="$(seq -s, 4.0 0.1 6.5),7,8,9,10,12"

doe() { # $1 = variant (huidig|onze), $2 = momenten, $3 = uit-map
  echo
  echo "############ variant=$1  uit=$3 ############"
  RG_LM_VARIANT="$1" RG_LM_MAX=30 RG_LM_UIT="$3" RG_LM_MOMENTEN="$2" \
    bash "$HIER/lm-meet.sh" 2>&1 | grep -E 'bench|kern in de runtime|ingezet|werkelijk gebruikt|exitcode' 
}

if [[ "$WIE" == "onze" || "$WIE" == "beide" ]]; then
  doe onze "$VENSTER" /tmp/lm-dicht-onze
fi
if [[ "$WIE" == "gesloten" || "$WIE" == "beide" ]]; then
  doe huidig "$RUIMER" /tmp/lm-dicht-gesloten
fi

echo
echo "############ SAMENVATTING ############"
for d in /tmp/lm-dicht-onze /tmp/lm-dicht-gesloten; do
  f="$d/meting-$( [[ $d == *onze ]] && echo onze || echo huidig ).txt"
  [[ -f "$f" ]] || continue
  echo "--- $f   (kern: $(cat "$d/werkelijk-sha" 2>/dev/null | cut -c1-12)) ---"
  awk '/^####|^-- \[0\]|^ +0x1402b4170|^PROCES WEG|verschenen/ {print}' "$f" \
    | sed 's/^/  /'
done
