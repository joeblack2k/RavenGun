#!/usr/bin/env bash
# lm-pakker.sh -- WINEDEBUG=+virtual op beide kernen, om te zien wat het spel met zijn
# geheugenbescherming doet en waar het onder onze kern misgaat.
set -u
HIER="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KANAAL="${RG_PAK_KANAAL:-+virtual}"

doe() { # $1 variant, $2 uit-map
  echo
  echo "############ $1  ($KANAAL)  uit=$2 ############"
  RG_LM_VARIANT="$1" RG_LM_WINEDEBUG="$KANAAL" RG_LM_MAX=20 RG_LM_UIT="$2" \
    RG_LM_MOMENTEN=6,12,18 bash "$HIER/lm-meet.sh" 2>&1 \
    | grep -E 'bench|kern in de runtime|ingezet|werkelijk gebruikt|exitcode'
  echo "  run.log: $(wc -l < "$2/run.log") regels"
}

doe huidig /tmp/lm-pak-gesloten
doe onze   /tmp/lm-pak-onze

echo
echo "############ SAMENVATTING ############"
for d in /tmp/lm-pak-gesloten /tmp/lm-pak-onze; do
  [[ -f "$d/run.log" ]] || continue
  echo "--- $d  (kern $(cat "$d/werkelijk-sha" 2>/dev/null | cut -c1-12)) ---"
  echo "  NtProtectVirtualMemory-regels : $(grep -ac 'NtProtectVirtualMemory' "$d/run.log")"
  echo "  NtAllocateVirtualMemory-regels: $(grep -ac 'NtAllocateVirtualMemory' "$d/run.log")"
  echo "  virtuele geheugen-regels totaal: $(grep -acE 'Nt(Protect|Allocate|Free)VirtualMemory' "$d/run.log")"
  echo "  eerste 12 regels:"
  grep -aE 'Nt(Protect|Allocate|Free)VirtualMemory' "$d/run.log" | head -12 | sed 's/^/    /'
done
