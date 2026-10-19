# shellcheck shell=bash
# Gedeeld door ab2.sh en mutex-hermeten.sh (review 14, ronde 2). Wordt gesourcet, niet gestart.
# Verwacht CTL, ENG en VIEW van het script dat dit inleest.
#
# Wat hier staat, is wat beide proeven op dezelfde manier moeten doen:
#   - de uitvoer niet in /tmp maar in de uit-map van de repo (tools/rg-rhio/out/ staat in .gitignore);
#   - de geinstalleerde drop-in (ENG) en die in de view (VIEW) bij het begin opzij zetten en bij elk
#     einde terugzetten (ook bij Ctrl-C of kill), want de proef zet er een proef-DLL overheen;
#   - de daemon nooit herstarten terwijl er een game draait;
#   - een foto telt alleen als "voorbij BOOTING" binnen de band 60 kB .. 900 kB (het BOOTING-scherm is
#     ~23 kB, het bureaublad ~1,46 MB en dat is GEEN spel).

AB_REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
AB_UIT_STANDAARD="$AB_REPO/tools/rg-rhio/out/ab-rhio"
# Het muis-geweer: de Steam Controller van de eigenaar. Op een andere machine RG_AB_MUIS zetten.
AB_MUIS="${RG_AB_MUIS:-usb-Valve_Software_Steam_Controller_Puck_FXB9955202923-if03-event-mouse}"
AB_TVENV="${RG_AB_TVENV:-/home/deck/projects/ravengun/tools/dev/rg-tv-env.sh}"
AB_RESERVE=""
AB_MARKER=""

# ja | nee | geen-foto, naar de grootte van de foto in bytes.
foto_oordeel() {
    case "${1:-}" in
        ''|0) echo geen-foto ;;
        *) if [ "$1" -gt 60000 ] && [ "$1" -lt 900000 ]; then echo ja; else echo nee; fi ;;
    esac
}

# Het aantal GELDIGE starts van een variant in een resultatenbestand: elke regel met die variant,
# behalve een start die ongeldig was omdat de daemon al bezet was (kolom 9). Een start waarbij de game
# stopte telt WEL: dat is juist een uitkomst.
tel_geldig() {   # $1 = bestand, $2 = variant
    awk -v v="$2" '$1==v && $9!="ongeldig-daemon-bezet"' "$1" 2>/dev/null | wc -l
}

# De eerste regel van "game status".
game_status() { "$CTL" game status 2>&1 | head -1; }

# Vrij = de daemon zegt zelf dat er geen game draait. Al het andere (een game, starten/stoppen, of de
# daemon antwoordt niet) telt als niet vrij.
game_vrij() {
    case "$(game_status)" in *"geen game"*) return 0 ;; esac
    return 1
}

# Aantoonbaar bezet: de daemon meldt zelf een lopende game of een start/stop.
game_bezet() {
    case "$(game_status)" in
        *"De game draait"*|*"wordt gestart"*|*"wordt gestopt"*|*"Wacht op de Move"*) return 0 ;;
    esac
    return 1
}

wacht_vrij() {
    for _ in $(seq 1 30); do
        game_vrij && return 0
        sleep 2
    done
    return 1
}

# Weigeren als er een game draait: dan geen DLL wisselen en de daemon niet herstarten (dat zou de game
# van een ander onder zijn handen weghalen).
weiger_als_niet_vrij() {
    game_vrij && return 0
    echo "geweigerd: er draait een game of de daemon antwoordt niet ($(game_status)); geen DLL gewisseld en de daemon niet herstart" >&2
    return 1
}

herstart_daemon() {
    weiger_als_niet_vrij || return 1
    systemctl --user restart ravengund >/dev/null 2>&1
}

# Een bestand terugzetten via een nieuw bestand en mv: een DLL die nog in een lopend proces geladen is,
# wordt zo niet onder dat proces overschreven.
zet_terug() {   # $1 = bron, $2 = doel
    cp -p "$1" "$2.ab-terug.$$" && mv -f "$2.ab-terug.$$" "$2"
}

# De DLL's opzij zetten in <uit>/reserve. Staat daar al een kopie (een vorige run is met kill -9 gestopt
# en zette niets terug), dan is DIE het origineel: de huidige DLL is dan nog een proef-DLL.
reserve_maak() {   # $1 = uit-map
    AB_RESERVE="$1/reserve"
    mkdir -p "$AB_RESERVE"
    local naam bron
    for naam in engine view; do
        if [ "$naam" = engine ]; then bron="$ENG"; else bron="$VIEW"; fi
        if [ -e "$AB_RESERVE/$naam.dll" ] || [ -e "$AB_RESERVE/$naam.geen" ]; then
            echo "reservekopie ($naam) uit een afgebroken run gevonden; die wordt straks teruggezet"
            continue
        fi
        if [ -f "$bron" ]; then cp -p "$bron" "$AB_RESERVE/$naam.dll"; else : > "$AB_RESERVE/$naam.geen"; fi
    done
}

reserve_terug() {
    [ -n "$AB_RESERVE" ] && [ -d "$AB_RESERVE" ] || return 0
    local naam bron
    for naam in engine view; do
        if [ "$naam" = engine ]; then bron="$ENG"; else bron="$VIEW"; fi
        if [ -f "$AB_RESERVE/$naam.dll" ]; then
            zet_terug "$AB_RESERVE/$naam.dll" "$bron" && rm -f "$AB_RESERVE/$naam.dll"
        elif [ -e "$AB_RESERVE/$naam.geen" ]; then
            rm -f "$bron" "$AB_RESERVE/$naam.geen"
        fi
    done
    rmdir "$AB_RESERVE" 2>/dev/null
    echo "drop-in teruggezet (engine en view)"
}

ab_einde() {
    [ -n "$AB_MARKER" ] && rm -f "$AB_MARKER"
    reserve_terug
}

# Slot, reservekopie en de val voor elk einde. Het slot eerst: een tweede instantie mag de reservekopie
# niet aanraken (die zou de proef-DLL van de eerste als origineel zien en die bij haar einde terugzetten).
ab_begin() {   # $1 = uit-map
    mkdir -p "$1"
    exec 9>"$1/ab-rhio.slot"
    if ! flock -n 9; then echo "er loopt al een A/B- of mutex-proef; deze stopt"; exit 0; fi
    reserve_maak "$1"
    trap ab_einde EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
}
