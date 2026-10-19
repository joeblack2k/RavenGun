#!/bin/bash
# Her-meting van de mutex-haken (review 12, punt 3): de haken zouden het spel meteen laten stoppen.
# Volgens de nieuwe regel: minstens 5 starts per variant, om en om, tegen een basislijn.
#   AAN  = rg-rhio-mutex.aan in de view (haken WaitForSingleObject/ReleaseMutex/CreateMutexA)
#   UIT  = geen marker (basislijn)
# De gebruikte DLL is de nieuwe (met de drie races gerepareerd).
#
# Gebruik: tools/dev/ab-rhio/mutex-hermeten.sh <pad naar rg-rhio.dll> [rondes]
# De uitvoer komt in $RG_AB_UIT (standaard tools/rg-rhio/out/ab-rhio in deze repo, in .gitignore).
# Zoals ab2.sh (gemeen.sh): de geinstalleerde drop-in en die in de view worden opzij gezet en bij elk
# einde teruggezet (ook de marker gaat dan weg), en draait er een game, dan weigert het script.
set -u
CTL="$HOME/.local/opt/ravengun/bin/ravengunctl"
ENG="$HOME/.local/opt/ravengun/patches/RobinHood/dropin/rg-rhio.dll"
VIEWMAP="$HOME/.local/share/ravengun/runtime/views/RobinHood/data"
VIEW="$VIEWMAP/rg-rhio.dll"
# shellcheck source=gemeen.sh
. "$(dirname "${BASH_SOURCE[0]}")/gemeen.sh"
DLL="${1:?gebruik: mutex-hermeten.sh <pad naar rg-rhio.dll> [rondes]}"
RONDES="${2:-6}"
OUT="${RG_AB_UIT:-$AB_UIT_STANDAARD}"
LOG="$VIEWMAP/rg-rhio.log"
RES="$OUT/mutex-resultaten.txt"
[ -f "$DLL" ] || { echo "mutex-hermeten: $DLL bestaat niet" >&2; exit 2; }

ab_begin "$OUT"
AB_MARKER="$VIEWMAP/rg-rhio-mutex.aan"   # weg bij elk einde, ook na Ctrl-C (ab_einde)
weiger_als_niet_vrij || exit 3
cp -f "$DLL" "$ENG"; cp -f "$DLL" "$VIEW"
SHA=$(sha1sum "$DLL" | cut -c1-8)
# Niet wissen: dit script wordt in stukken gedraaid (een lange reeks wordt door een externe limiet
# afgebroken), dus de uitslagen blijven staan en het aantal rondes is per stuk te kiezen.
touch "$RES"

# De rondenummering loopt PER VARIANT door waar het bestand gebleven is (review 14, ronde 2): eerder
# kwam het nummer uit regels/2+1, en na een afbreking tussen UIT en AAN kwam hetzelfde nummer twee keer.
echo "nu: UIT=$(tel_geldig "$RES" UIT) AAN=$(tel_geldig "$RES" AAN) rondes; er komen er $RONDES per variant bij"
for _ in $(seq 1 "$RONDES"); do
    for v in UIT AAN; do
        r=$(( $(tel_geldig "$RES" "$v") + 1 ))
        weiger_als_niet_vrij || exit 3
        if [ "$v" = AAN ]; then printf '1\n' > "$AB_MARKER"; else rm -f "$AB_MARKER"; fi
        rm -f "$LOG"
        herstart_daemon || exit 3
        sleep 7
        "$CTL" mouse-gun --player 1 --mouse "$AB_MUIS" >/dev/null 2>&1
        "$CTL" game start RobinHood >/dev/null 2>&1
        sleep 90
        PNG="$OUT/mutex-$v-$r.png"
        "$AB_TVENV" --no-hold -- spectacle -b -n -f -o "$PNG" >/dev/null 2>&1
        SZ=$(stat -c%s "$PNG" 2>/dev/null || echo 0)
        ST=$(game_status)
        case "$ST" in *"De game draait"*) DRAAIT=ja;; *) DRAAIT=nee;; esac
        # Dezelfde band als ab2.sh: het bureaublad (~1,46 MB) is geen spel.
        OK=$(foto_oordeel "$SZ")
        LR=$(wc -l < "$LOG" 2>/dev/null || echo 0)
        MUT=$(rg -c 'mutex: ' "$LOG" 2>/dev/null || echo 0)
        echo "$v $r $SHA $SZ $OK $DRAAIT $LR $MUT" >> "$RES"
        echo "$(date '+%H:%M:%S') haken=$v ronde $r: foto=$SZ ok=$OK draait=$DRAAIT logregels=$LR mutexregels=$MUT"
        "$CTL" game stop >/dev/null 2>&1
        wacht_vrij
        sleep 6
    done
done
rm -f "$AB_MARKER"
echo "KLAAR"
