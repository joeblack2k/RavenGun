#!/bin/bash
# Her-meting van de mutex-haken (review 12, punt 3): de haken zouden het spel meteen laten stoppen.
# Volgens de nieuwe regel: minstens 5 starts per variant, om en om, tegen een basislijn.
#   AAN  = rg-rhio-mutex.aan in de view (haken WaitForSingleObject/ReleaseMutex/CreateMutexA)
#   UIT  = geen marker (basislijn)
# De gebruikte DLL is de nieuwe (met de drie races gerepareerd).
set -u
CTL="$HOME/.local/opt/ravengun/bin/ravengunctl"
ENG="$HOME/.local/opt/ravengun/patches/RobinHood/dropin/rg-rhio.dll"
VIEWMAP="$HOME/.local/share/ravengun/runtime/views/RobinHood/data"
DLL="${1:?gebruik: mutex-hermeten.sh <pad naar rg-rhio.dll> [rondes]}"
RONDES="${2:-6}"
OUT="${RG_AB_UIT:-/tmp/rg-ab}"
LOG="$VIEWMAP/rg-rhio.log"

wacht_vrij() {
    for _ in $(seq 1 30); do
        s=$("$CTL" game status 2>&1 | head -1)
        case "$s" in *"geen game"*) return 0;; esac
        sleep 2
    done
    return 0
}

cp -f "$DLL" "$ENG"; cp -f "$DLL" "$VIEWMAP/rg-rhio.dll"
SHA=$(sha1sum "$DLL" | cut -c1-8)
rm -f "$OUT/mutex-resultaten.txt"

for r in $(seq 1 "$RONDES"); do
    for v in UIT AAN; do
        if [ "$v" = AAN ]; then printf '1\n' > "$VIEWMAP/rg-rhio-mutex.aan"; else rm -f "$VIEWMAP/rg-rhio-mutex.aan"; fi
        rm -f "$LOG"
        systemctl --user restart ravengund >/dev/null 2>&1
        sleep 7
        "$CTL" mouse-gun --player 1 --mouse usb-Valve_Software_Steam_Controller_Puck_FXB9955202923-if03-event-mouse >/dev/null 2>&1
        "$CTL" game start RobinHood >/dev/null 2>&1
        sleep 90
        PNG="$OUT/mutex-$v-$r.png"
        /home/deck/projects/ravengun/tools/dev/rg-tv-env.sh --no-hold -- spectacle -b -n -f -o "$PNG" >/dev/null 2>&1
        SZ=$(stat -c%s "$PNG" 2>/dev/null || echo 0)
        ST=$("$CTL" game status 2>&1 | head -1)
        case "$ST" in *"De game draait"*) DRAAIT=ja;; *) DRAAIT=nee;; esac
        case "$SZ" in ''|0) OK=geen-foto;; *) if [ "$SZ" -gt 60000 ]; then OK=ja; else OK=nee; fi;; esac
        LR=$(wc -l < "$LOG" 2>/dev/null || echo 0)
        MUT=$(rg -c 'mutex: ' "$LOG" 2>/dev/null || echo 0)
        echo "$v $r $SHA $SZ $OK $DRAAIT $LR $MUT" >> "$OUT/mutex-resultaten.txt"
        echo "$(date '+%H:%M:%S') haken=$v ronde $r: foto=$SZ ok=$OK draait=$DRAAIT logregels=$LR mutexregels=$MUT"
        "$CTL" game stop >/dev/null 2>&1
        wacht_vrij
        sleep 6
    done
done
rm -f "$VIEWMAP/rg-rhio-mutex.aan"
echo "KLAAR"
