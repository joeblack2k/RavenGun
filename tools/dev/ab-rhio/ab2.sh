#!/bin/bash
# A/B-proef rg-rhio (opdracht leiding): OLD vs NEW vs NOSTRIP, 10 starts per variant, om en om.
# Herschreven zodat hij HERVATBAAR is: de telling per variant komt uit resultaten.txt, dus een
# afgebroken ronde gaat verder waar hij was. Bovendien stopt hij zelf na een tijdsbudget
# (BUDGET seconden, standaard 480) zodat een lange reeks niet door een externe limiet wordt gekapt.
#
# Per start: variant-DLL naar de engine-drop-in EN de view, daemon herstarten, muis-geweer zetten,
# starten, 90 s wachten, foto, en twee uitkomsten opschrijven:
#   ok     = voorbij BOOTING naar de bordframes (foto duidelijk groter dan het BOOTING-scherm)
#   draait = de game leeft nog (de bewering was dat NEW het spel meteen laat stoppen)
# De .aan-markers zijn voor deze proef alle drie weggehaald en blijven weg (identiek voor elke variant).
# Gebruik:
#   tools/dev/ab-rhio/ab2.sh                 (doorgaan tot DOEL starts per variant; BUDGET seconden per keer)
#   DOEL=5 BUDGET=480 tools/dev/ab-rhio/ab2.sh
# De uitvoer (foto's en resultaten.txt) komt in $RG_AB_UIT (standaard /tmp/rg-ab). De variant-DLL's
# staan in $RG_AB_UIT/old-rg-rhio.dll (de oude, eerst zelf kopieren) en in de bronboom van de leiding.
set -u
CTL="$HOME/.local/opt/ravengun/bin/ravengunctl"
ENG="$HOME/.local/opt/ravengun/patches/RobinHood/dropin/rg-rhio.dll"
VIEWMAP="$HOME/.local/share/ravengun/runtime/views/RobinHood/data"
VIEW="$VIEWMAP/rg-rhio.dll"
LOG="$VIEWMAP/rg-rhio.log"
OUT="${RG_AB_UIT:-/tmp/rg-ab}"
OLD="$OUT/old-rg-rhio.dll"
NEW="$HOME/projects/ravengun-claude/tools/rg-rhio/out/rg-rhio.dll"
NOSTRIP="$HOME/projects/ravengun-claude/tools/rg-rhio/out/rg-rhio-nostrip.dll"
DOEL="${DOEL:-10}"
BUDGET="${BUDGET:-480}"
WACHT="${WACHT:-90}"
mkdir -p "$OUT"
touch "$OUT/resultaten.txt"
START=$(date +%s)

# Een tweede instantie naast de eerste laat twee games tegelijk draaien en vervuilt de meting (dat
# gebeurde ook echt: een run kreeg het logboekprofiel van een andere variant). Een slotbestand met
# flock voorkomt dat.
exec 9>"$OUT/ab2.slot"
if ! flock -n 9; then echo "er loopt al een A/B-instantie; deze stopt"; exit 0; fi

tel() { awk -v v="$1" '$1==v' "$OUT/resultaten.txt" 2>/dev/null | wc -l; }

wacht_vrij() {
    for _ in $(seq 1 30); do
        s=$("$CTL" game status 2>&1 | head -1)
        case "$s" in *"geen game"*) return 0;; esac
        sleep 2
    done
    return 0
}

while :; do
    # de variant met de laagste telling die het doel nog niet haalt
    keuze=""; laagst=999999
    for v in OLD NEW NOSTRIP; do
        n=$(tel "$v")
        if [ "$n" -lt "$DOEL" ] && [ "$n" -lt "$laagst" ]; then laagst=$n; keuze=$v; fi
    done
    [ -z "$keuze" ] && { echo "alle varianten op $DOEL"; break; }
    if [ $(( $(date +%s) - START )) -gt "$BUDGET" ]; then
        echo "budget van $BUDGET s op; volgende keer verder (telling: OLD=$(tel OLD) NEW=$(tel NEW) NOSTRIP=$(tel NOSTRIP))"
        break
    fi
    ronde=$((laagst + 1))
    case "$keuze" in
        OLD)     SRC="$OLD";;
        NEW)     SRC="$NEW";;
        NOSTRIP) SRC="$NOSTRIP";;
    esac
    if [ ! -f "$SRC" ]; then echo "$keuze: bron $SRC ontbreekt"; break; fi
    SHA=$(sha1sum "$SRC" | cut -c1-8)
    cp -f "$SRC" "$ENG"; cp -f "$SRC" "$VIEW"
    rm -f "$LOG"
    systemctl --user restart ravengund >/dev/null 2>&1
    sleep 7
    "$CTL" mouse-gun --player 1 --mouse usb-Valve_Software_Steam_Controller_Puck_FXB9955202923-if03-event-mouse >/dev/null 2>&1
    "$CTL" game start RobinHood >/dev/null 2>&1
    sleep "$WACHT"
    PNG="$OUT/$keuze-$ronde.png"
    /home/deck/projects/ravengun/tools/dev/rg-tv-env.sh --no-hold -- spectacle -b -n -f -o "$PNG" >/dev/null 2>&1
    SZ=$(stat -c%s "$PNG" 2>/dev/null || echo 0)
    ST=$("$CTL" game status 2>&1 | head -1)
    case "$ST" in *"De game draait"*) DRAAIT=ja;; *) DRAAIT=nee;; esac
    # Het BOOTING-scherm is ~23 kB, de bordframes zijn groter, en het bureaublad is ~1,46 MB: dat is
    # GEEN spel. Alleen de band daartussen telt als "voorbij BOOTING".
    case "$SZ" in ''|0) OK=geen-foto;; *) if [ "$SZ" -gt 60000 ] && [ "$SZ" -lt 900000 ]; then OK=ja; else OK=nee; fi;; esac
    LR=$(wc -l < "$LOG" 2>/dev/null || echo 0)
    BORD=$(rg -c 'ReadFile\(bord' "$LOG" 2>/dev/null || echo 0)
    if [ "$DRAAIT" != ja ]; then
        # De game liep niet (daemon nog bezet, of iets anders liep ernaast): deze run zegt niets over de
        # DLL en gaat NIET de telling in. Opnieuw proberen, met een bovengrens tegen een lus.
        echo "$(date '+%H:%M:%S') $keuze ronde $ronde ONGELDIG (game liep niet, foto=$SZ); opnieuw"
        "$CTL" game stop >/dev/null 2>&1
        wacht_vrij
        sleep 6
        pogingen=$(( ${pogingen:-0} + 1 ))
        if [ "$pogingen" -gt 6 ]; then echo "te veel ongeldige runs; stoppen"; break 2; fi
        continue
    fi
    echo "$keuze $ronde $SHA $SZ $OK $DRAAIT $LR $BORD" >> "$OUT/resultaten.txt"
    echo "$(date '+%H:%M:%S') $keuze ronde $ronde: foto=$SZ ok=$OK draait=$DRAAIT logregels=$LR bordlezers=$BORD"
    "$CTL" game stop >/dev/null 2>&1
    wacht_vrij
    sleep 6
done
echo "KLAAR (OLD=$(tel OLD) NEW=$(tel NEW) NOSTRIP=$(tel NOSTRIP))"
