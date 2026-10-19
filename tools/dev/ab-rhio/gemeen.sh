# shellcheck shell=bash
# Gedeeld door ab2.sh en mutex-hermeten.sh (review 14, ronde 2). Wordt gesourcet, niet gestart.
# Verwacht CTL, ENG en VIEW van het script dat dit inleest.
#
# Wat hier staat, is wat beide proeven op dezelfde manier moeten doen:
#   - de uitvoer (foto's, uitslagen) niet in /tmp maar in de uit-map van de repo (tools/rg-rhio/out/
#     staat in .gitignore);
#   - de geinstalleerde drop-in (ENG) en die in de view (VIEW) bij het begin opzij zetten en bij elk
#     einde terugzetten (ook bij Ctrl-C of kill), want de proef zet er een proef-DLL overheen;
#   - het slot en die reservekopie staan op EEN vaste plek per gebruiker (AB_STAAT), want ze horen bij
#     de engine die ze beschermen en niet bij een kloon of een RG_AB_UIT: met een slot per uit-map kon
#     een tweede kloon starten terwijl de eerste een proef-DLL in de engine had, en die proef-DLL dan
#     als "origineel" bewaren en voorgoed terugzetten (herreview review 14, ronde 2);
#   - de daemon nooit herstarten terwijl er een game draait;
#   - een foto telt alleen als "voorbij BOOTING" binnen de band 60 kB .. 900 kB (het BOOTING-scherm is
#     ~23 kB, het bureaublad ~1,46 MB en dat is GEEN spel).

AB_REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
AB_UIT_STANDAARD="$AB_REPO/tools/rg-rhio/out/ab-rhio"
# Het muis-geweer: de Steam Controller van de eigenaar. Op een andere machine RG_AB_MUIS zetten.
AB_MUIS="${RG_AB_MUIS:-usb-Valve_Software_Steam_Controller_Puck_FXB9955202923-if03-event-mouse}"
AB_TVENV="${RG_AB_TVENV:-/home/deck/projects/ravengun/tools/dev/rg-tv-env.sh}"
# Slot en reservekopie: vast per gebruiker, net als de engine in $HOME/.local/opt/ravengun.
AB_STAAT="${XDG_STATE_HOME:-$HOME/.local/state}/ravengun/ab-rhio"
AB_RESERVE=""
AB_MARKER=""
# De sha1 van de proef-DLL's van deze run (OLD/NEW/NOSTRIP, of de DLL van mutex-hermeten). Het script
# vult dit voor ab_begin; reserve_maak herkent er een achtergebleven proef-DLL aan.
AB_PROEF_SHAS=()

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

# Een bestand neerzetten via een nieuw bestand en mv (een nieuw inode): een DLL die nog in een lopend
# proces geladen is, wordt zo niet onder dat proces overschreven. Voor het terugzetten EN voor het
# neerzetten van een proef-DLL.
zet_terug() {   # $1 = bron, $2 = doel
    if cp -p "$1" "$2.ab-nieuw.$$" && mv -f "$2.ab-nieuw.$$" "$2"; then return 0; fi
    rm -f "$2.ab-nieuw.$$"
    return 1
}

sha_van() { sha1sum "$1" 2>/dev/null | cut -d" " -f1; }

is_proef_sha() {   # $1 = sha1
    local s
    [ -n "$1" ] || return 1
    for s in "${AB_PROEF_SHAS[@]}"; do [ "$s" = "$1" ] && return 0; done
    return 1
}

# De DLL's opzij zetten in AB_STAAT/reserve: <naam>.dll met de sha1 ernaast (<naam>.dll.sha1), of
# <naam>.geen als er geen DLL was. Beide via een tijdelijk bestand en mv, de sha1 als laatste: een
# reservekopie zonder kloppende sha1 is half geschreven.
#
# Staat er nog een reservekopie (een vorige run is met kill -9 gestopt), dan geldt die ALLEEN als het
# origineel als de huidige DLL een proef-DLL van deze run is (dan zette die run hem neer) en de
# reservekopie heel is. Anders is hij oud (bijvoorbeeld: daarna is de engine gewoon bijgewerkt) en gaat
# hij weg, met een waarschuwing. Is de huidige DLL een proef-DLL en is er geen geldige reservekopie, dan
# weten we het origineel niet: weigeren (behalve met RG_AB_HUIDIG_IS_ORIGINEEL=1, als de proef-DLL ook
# echt de geinstalleerde is, bijvoorbeeld OLD).
reserve_maak() {
    AB_RESERVE="$AB_STAAT/reserve"
    mkdir -p "$AB_RESERVE"
    local naam bron res huidig bewaard geldig
    local -a kopieer=()
    for naam in engine view; do
        if [ "$naam" = engine ]; then bron="$ENG"; else bron="$VIEW"; fi
        res="$AB_RESERVE/$naam.dll"
        huidig=""
        [ -f "$bron" ] && huidig=$(sha_van "$bron")
        if [ -e "$res" ] || [ -e "$res.sha1" ] || [ -e "$AB_RESERVE/$naam.geen" ]; then
            geldig=nee
            if is_proef_sha "$huidig"; then
                if [ -e "$AB_RESERVE/$naam.geen" ] && [ ! -e "$res" ]; then
                    geldig=ja
                elif [ -f "$res" ] && [ -f "$res.sha1" ]; then
                    bewaard=$(cat "$res.sha1")
                    [ -n "$bewaard" ] && [ "$bewaard" = "$(sha_van "$res")" ] && geldig=ja
                fi
            fi
            if [ "$geldig" = ja ]; then
                echo "reservekopie ($naam) uit een afgebroken run gevonden en heel, en de huidige $naam-DLL is een proef-DLL; die reservekopie wordt straks teruggezet"
                continue
            fi
            echo "waarschuwing: oude reservekopie ($naam) in $AB_RESERVE weggegooid (de huidige DLL ${huidig:-(afwezig)} is geen proef-DLL van deze run, of de kopie is niet heel)" >&2
            rm -f "$res" "$res.sha1" "$AB_RESERVE/$naam.geen"
        fi
        if is_proef_sha "$huidig" && [ "${RG_AB_HUIDIG_IS_ORIGINEEL:-0}" != 1 ]; then
            echo "geweigerd: de huidige $naam-DLL ($bron, sha1 $huidig) is een proef-DLL van deze run en er is geen geldige reservekopie. Zet eerst de vastgelegde DLL terug (patches/RobinHood/dropin/rg-rhio.dll uit git), of zet RG_AB_HUIDIG_IS_ORIGINEEL=1 als deze DLL echt de geinstalleerde is." >&2
            exit 3
        fi
        kopieer+=("$naam")
    done
    # Pas nu (beide gecontroleerd, niets geweigerd) de kopieen maken.
    for naam in "${kopieer[@]}"; do
        if [ "$naam" = engine ]; then bron="$ENG"; else bron="$VIEW"; fi
        res="$AB_RESERVE/$naam.dll"
        if [ -f "$bron" ]; then
            if ! { cp -p "$bron" "$res.nieuw.$$" && mv -f "$res.nieuw.$$" "$res" &&
                   printf '%s\n' "$(sha_van "$res")" > "$res.sha1.nieuw.$$" && mv -f "$res.sha1.nieuw.$$" "$res.sha1"; }; then
                echo "kon geen reservekopie van $bron maken; gestopt" >&2
                exit 3
            fi
        else
            : > "$AB_RESERVE/$naam.geen"
        fi
    done
}

reserve_terug() {
    [ -n "$AB_RESERVE" ] && [ -d "$AB_RESERVE" ] || return 0
    local naam bron mis=0
    for naam in engine view; do
        if [ "$naam" = engine ]; then bron="$ENG"; else bron="$VIEW"; fi
        if [ -f "$AB_RESERVE/$naam.dll" ]; then
            if zet_terug "$AB_RESERVE/$naam.dll" "$bron"; then
                rm -f "$AB_RESERVE/$naam.dll" "$AB_RESERVE/$naam.dll.sha1"
            else
                echo "FOUT: kon $bron niet terugzetten; de reservekopie blijft in $AB_RESERVE/$naam.dll" >&2
                mis=1
            fi
        elif [ -e "$AB_RESERVE/$naam.geen" ]; then
            if rm -f "$bron"; then rm -f "$AB_RESERVE/$naam.geen"; else mis=1; fi
        fi
    done
    rmdir "$AB_RESERVE" 2>/dev/null
    if [ "$mis" = 0 ]; then
        echo "drop-in teruggezet (engine en view)"
    else
        echo "drop-in NIET volledig teruggezet (zie hierboven)" >&2
    fi
}

ab_einde() {
    [ -n "$AB_MARKER" ] && rm -f "$AB_MARKER"
    reserve_terug
}

# Slot, reservekopie en de val voor elk einde. Het slot eerst, op de vaste plek: een tweede instantie
# (ook uit een andere kloon of met een andere RG_AB_UIT) mag de reservekopie niet aanraken -- die zou de
# proef-DLL van de eerste als origineel zien en die bij haar einde terugzetten. AB_PROEF_SHAS eerst vullen.
ab_begin() {   # $1 = uit-map (foto's en uitslagen)
    mkdir -p "$1" "$AB_STAAT"
    exec 9>"$AB_STAAT/ab-rhio.slot"
    if ! flock -n 9; then
        echo "er loopt al een A/B- of mutex-proef (slot $AB_STAAT/ab-rhio.slot); deze stopt" >&2
        exit 3
    fi
    reserve_maak
    trap ab_einde EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
}
