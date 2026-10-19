#!/bin/bash
# A/B-proef rg-rhio (opdracht leiding): OLD vs NEW vs NOSTRIP, 10 starts per variant, om en om.
# Herschreven zodat hij HERVATBAAR is: de telling per variant komt uit resultaten.txt, dus een
# afgebroken ronde gaat verder waar hij was. Bovendien stopt hij zelf na een tijdsbudget
# (BUDGET seconden, standaard 480) zodat een lange reeks niet door een externe limiet wordt gekapt.
#
# Per start: variant-DLL naar de engine-drop-in EN de view, daemon herstarten, muis-geweer zetten,
# starten, 90 s wachten, foto, en de uitkomst opschrijven:
#   ok     = voorbij BOOTING naar de bordframes (foto in de band 60 kB .. 900 kB, zie gemeen.sh)
#   draait = de game leeft nog (de bewering was dat NEW het spel meteen laat stoppen)
#   reden  = draait | gestopt | ongeldig-daemon-bezet | ongeldig-start-mislukt
# ELKE start komt in resultaten.txt, ook een start waarbij de game stopte: dat is juist de uitkomst die
# de proef zoekt (review 14, ronde 2; eerder werd zo'n start weggegooid, zodat "het spel bleef in alle
# runs draaien" per constructie waar was). Alleen een start waarbij de daemon AANTOONBAAR al bezet was
# (voor de start meldde hij een lopende game, of de start werd daarom geweigerd) heet
# ongeldig-daemon-bezet. Een start die de daemon zelf niet rond kreeg (ravengunctl gaf een fout, of de
# status zegt "Starten is mislukt.") heet alleen ongeldig-start-mislukt als rg-rhio.dll nooit laadde
# (geen rg-rhio.log); met een logboek is het gestopt, want dan viel de game met deze DLL om. De
# uitvoer van een mislukte start komt in de uitvoer van dit script. Beide ongeldige soorten tellen niet mee voor DOEL en worden opnieuw gedaan,
# met een bovengrens van zes op rij; maak-tabel.py telt ze per variant (een variant die steeds niet
# start, valt daar dus op).
# De .aan-markers zijn voor deze proef alle drie weggehaald en blijven weg (identiek voor elke variant).
#
# Gebruik:
#   RG_AB_NEW=<nieuwe rg-rhio.dll> RG_AB_NOSTRIP=<ongestripte rg-rhio.dll> tools/dev/ab-rhio/ab2.sh
#   DOEL=5 BUDGET=480 RG_AB_NEW=... RG_AB_NOSTRIP=... tools/dev/ab-rhio/ab2.sh
# De uitvoer (foto's en resultaten.txt) komt in $RG_AB_UIT (standaard tools/rg-rhio/out/ab-rhio in deze
# repo, en die map staat in .gitignore). De oude DLL staat in $RG_AB_UIT/old-rg-rhio.dll (eerst zelf
# kopieren). RG_AB_MUIS kiest het muis-geweer (standaard de Steam Controller van de eigenaar).
# De geinstalleerde drop-in en die in de view worden bij het begin opzij gezet en bij elk einde
# teruggezet; draait er een game, dan weigert het script (geen DLL-wissel, geen herstart). Slot en
# reservekopie staan vast in ~/.local/state/ravengun/ab-rhio (zie gemeen.sh). Is de geinstalleerde DLL
# een variant van deze run die niet als oplevering in git staat en geen geldige reservekopie heeft, dan
# weigert het script (het origineel is dan onbekend) en zegt het hoe je de vastgelegde DLL terugzet.
# Tabel: tools/dev/ab-rhio/maak-tabel.py $RG_AB_UIT/resultaten.txt
set -u
CTL="$HOME/.local/opt/ravengun/bin/ravengunctl"
ENG="$HOME/.local/opt/ravengun/patches/RobinHood/dropin/rg-rhio.dll"
VIEWMAP="$HOME/.local/share/ravengun/runtime/views/RobinHood/data"
VIEW="$VIEWMAP/rg-rhio.dll"
LOG="$VIEWMAP/rg-rhio.log"
# shellcheck source=gemeen.sh
. "$(dirname "${BASH_SOURCE[0]}")/gemeen.sh"
OUT="${RG_AB_UIT:-$AB_UIT_STANDAARD}"
OLD="$OUT/old-rg-rhio.dll"
NEW="${RG_AB_NEW:-}"
NOSTRIP="${RG_AB_NOSTRIP:-}"
DOEL="${DOEL:-10}"
BUDGET="${BUDGET:-480}"
WACHT="${WACHT:-90}"
RES="$OUT/resultaten.txt"

ontbreekt=0
for paar in "OLD:$OLD:kopieer de oude DLL eerst naar $OLD" \
            "NEW:$NEW:zet RG_AB_NEW=<pad naar de nieuwe rg-rhio.dll>" \
            "NOSTRIP:$NOSTRIP:zet RG_AB_NOSTRIP=<pad naar de ongestripte rg-rhio.dll>"; do
    naam="${paar%%:*}"; rest="${paar#*:}"; pad="${rest%%:*}"; hulp="${rest#*:}"
    if [ -z "$pad" ] || [ ! -f "$pad" ]; then
        echo "ab2: $naam-DLL ontbreekt (${pad:-niet gezet}); $hulp" >&2
        ontbreekt=1
    fi
done
[ "$ontbreekt" = 0 ] || exit 2

# Een tweede instantie naast de eerste laat twee games tegelijk draaien en vervuilt de meting (dat
# gebeurde ook echt: een run kreeg het logboekprofiel van een andere variant). Het slot (flock) in
# ab_begin voorkomt dat; daarna de reservekopie van de drop-ins en de val die ze terugzet.
AB_PROEF_SHAS=("$(sha_van "$OLD")" "$(sha_van "$NEW")" "$(sha_van "$NOSTRIP")")
ab_begin "$OUT"
touch "$RES"
START=$(date +%s)

tel() { tel_geldig "$RES" "$1"; }

pogingen=0
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
    # Eerst kijken of er een game draait: dan geen DLL wisselen en de daemon niet herstarten.
    weiger_als_niet_vrij || exit 3
    # Via een nieuw bestand en mv, niet cp -f in hetzelfde inode (zie plaats_proef: met een regel in het grootboek).
    plaats_proef "$SRC" "$ENG" engine && plaats_proef "$SRC" "$VIEW" view || { echo "kon $SRC niet neerzetten; gestopt" >&2; exit 3; }
    rm -f "$LOG"
    herstart_daemon || exit 3
    sleep 7
    "$CTL" mouse-gun --player 1 --mouse "$AB_MUIS" >/dev/null 2>&1
    # Was de daemon voor de start al bezet (iemand anders startte net iets), dan zegt deze start niets
    # over de DLL. Alleen dan, en als de start daarom geweigerd wordt, heet hij ongeldig.
    BEZET=nee
    game_bezet && BEZET=ja
    STARTUIT=$("$CTL" game start RobinHood 2>&1)
    STARTRC=$?
    case "$STARTUIT" in *"al een game"*) BEZET=ja;; esac
    sleep "$WACHT"
    PNG="$OUT/$keuze-$ronde.png"
    "$AB_TVENV" --no-hold -- spectacle -b -n -f -o "$PNG" >/dev/null 2>&1
    SZ=$(stat -c%s "$PNG" 2>/dev/null || echo 0)
    ST=$(game_status)
    case "$ST" in *"De game draait"*) DRAAIT=ja;; *) DRAAIT=nee;; esac
    OK=$(foto_oordeel "$SZ")
    LR=$(wc -l < "$LOG" 2>/dev/null || echo 0)
    BORD=$(rg -c 'ReadFile\(bord' "$LOG" 2>/dev/null || echo 0)
    if [ "$BEZET" = ja ]; then REDEN=ongeldig-daemon-bezet
    elif [ "$DRAAIT" = ja ]; then REDEN=draait
    elif [ "$STARTRC" != 0 ] || case "$ST" in *"Starten is mislukt"*) true;; *) false;; esac; then
        # De daemon meldt een mislukte start. Schreef rg-rhio.dll voor deze start een logboek (het is
        # voor de start gewist), dan was de DLL geladen en is "de game viel meteen om" juist de uitkomst
        # die de proef zoekt: gestopt, tegen deze variant. Alleen als de DLL nooit laadde, zegt de start
        # niets over de DLL (herreview 2).
        if [ -s "$LOG" ]; then REDEN=gestopt; else REDEN=ongeldig-start-mislukt; fi
    else REDEN=gestopt
    fi
    # variant ronde sha foto ok draait logregels bordlezers reden
    echo "$keuze $ronde $SHA $SZ $OK $DRAAIT $LR $BORD $REDEN" >> "$RES"
    echo "$(date '+%H:%M:%S') $keuze ronde $ronde: foto=$SZ ok=$OK draait=$DRAAIT logregels=$LR bordlezers=$BORD reden=$REDEN"
    [ "$STARTRC" != 0 ] && echo "  start (rc=$STARTRC): $STARTUIT; status: $ST"
    case "$REDEN" in ongeldig-*)
        # Opnieuw proberen met een bovengrens tegen een lus: zes ONGELDIGE starts OP RIJ. De teller gaat
        # bij elke geldige start terug naar nul (eerder liep hij over de hele reeks door).
        pogingen=$((pogingen + 1))
        if [ "$pogingen" -gt 6 ]; then echo "te veel ongeldige runs op rij (laatste: $REDEN); stoppen"; break; fi
        # Bij een bezette daemon is de game die draait niet van ons (de daemon weigerde onze start):
        # niet stoppen, alleen wachten tot hij vrij is; is hij dat niet, dan weigert de volgende ronde.
        # Een mislukte start is wel van ons: die netjes stoppen.
        [ "$REDEN" = ongeldig-start-mislukt ] && "$CTL" game stop >/dev/null 2>&1
        wacht_vrij
        sleep 6
        continue
    esac
    pogingen=0
    "$CTL" game stop >/dev/null 2>&1
    wacht_vrij
    sleep 6
done
echo "KLAAR (OLD=$(tel OLD) NEW=$(tel NEW) NOSTRIP=$(tel NOSTRIP))"
