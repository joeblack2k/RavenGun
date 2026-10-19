# shellcheck shell=bash
# Gedeeld door ab2.sh en mutex-hermeten.sh (review 14, ronde 2). Wordt gesourcet, niet gestart.
# Verwacht CTL, ENG en VIEW van het script dat dit inleest.
#
# Wat hier staat, is wat beide proeven op dezelfde manier moeten doen:
#   - de uitvoer (foto's, uitslagen) niet in /tmp maar in de uit-map van de repo (tools/rg-rhio/out/
#     staat in .gitignore);
#   - de geinstalleerde drop-in (ENG) en die in de view (VIEW) bij het begin opzij zetten en bij elk
#     einde terugzetten (ook bij Ctrl-C of kill), want de proef zet er een proef-DLL overheen;
#   - het slot, die reservekopie en het grootboek staan op EEN vaste plek per gebruiker (AB_STAAT), want ze horen bij
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
# Slot, reservekopie en grootboek: vast per gebruiker, net als de engine in $HOME/.local/opt/ravengun.
# Bewust uit $HOME en niet uit XDG_STATE_HOME: twee aanroepen met een andere XDG_STATE_HOME kregen anders
# twee sloten voor dezelfde engine (herreview 2).
AB_STAAT="$HOME/.local/state/ravengun/ab-rhio"
AB_RESERVE=""
AB_MARKER=""
# De sha1 van de proef-DLL's van deze run (OLD/NEW/NOSTRIP, of de DLL van mutex-hermeten). Het script
# vult dit voor ab_begin; reserve_maak weigert een huidige DLL die een van deze is en niet als
# vastgelegde oplevering bekend is (want dan is het origineel onbekend).
AB_PROEF_SHAS=()

# ja | nee | geen-foto, naar de grootte van de foto in bytes.
foto_oordeel() {
    case "${1:-}" in
        ''|0) echo geen-foto ;;
        *) if [ "$1" -gt 60000 ] && [ "$1" -lt 900000 ]; then echo ja; else echo nee; fi ;;
    esac
}

# Het aantal GELDIGE starts van een variant in een resultatenbestand: elke regel met die variant,
# behalve een ongeldige start (kolom 9 begint met ongeldig-: de daemon was al bezet, of de start zelf
# mislukte). Een start waarbij de game stopte telt WEL: dat is juist een uitkomst.
tel_geldig() {   # $1 = bestand, $2 = variant
    awk -v v="$2" '$1==v && $9 !~ /^ongeldig-/' "$1" 2>/dev/null | wc -l
}

# De eerste regel van "game status".
game_status() { "$CTL" game status 2>&1 | head -1; }

# Vrij = de daemon zegt zelf dat er geen game draait, of dat de vorige start mislukte ("Starten is
# mislukt.": dan draait er niets en de herstart van de daemon ruimt die melding op). Al het andere (een
# game, starten/stoppen, of de daemon antwoordt niet) telt als niet vrij.
game_vrij() {
    case "$(game_status)" in *"geen game"*|*"Starten is mislukt"*) return 0 ;; esac
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
# proces geladen is, wordt zo niet onder dat proces overschreven. Voor het terugzetten; een proef-DLL
# gaat via plaats_proef (zelfde mv, plus het grootboek).
zet_terug() {   # $1 = bron, $2 = doel
    if cp -p "$1" "${2:?}.ab-nieuw.$$" && mv -f "${2:?}.ab-nieuw.$$" "$2"; then return 0; fi
    rm -f "${2:?}.ab-nieuw.$$"
    return 1
}

sha_van() { sha1sum "$1" 2>/dev/null | cut -d" " -f1; }

is_proef_sha() {   # $1 = sha1
    local s
    [ -n "$1" ] || return 1
    for s in "${AB_PROEF_SHAS[@]}"; do [ "$s" = "$1" ] && return 0; done
    return 1
}

# Bekende goede originelen: elke versie van patches/RobinHood/dropin/rg-rhio.dll die ooit in de
# geschiedenis van deze kloon is vastgelegd (git rev-list HEAD -- <pad>). Een DLL met zo'n sha1 is een
# echte oplevering en mag dus altijd als origineel bewaard worden. Een keer per run berekend.
AB_GOED_SHAS=()
AB_GOED_GELADEN=""
goede_shas_laden() {
    [ -n "$AB_GOED_GELADEN" ] && return 0
    AB_GOED_GELADEN=1
    local pad=patches/RobinHood/dropin/rg-rhio.dll c
    while read -r c; do
        git -C "$AB_REPO" cat-file -e "$c:$pad" 2>/dev/null || continue
        AB_GOED_SHAS+=("$(git -C "$AB_REPO" show "$c:$pad" | sha1sum | cut -d" " -f1)")
    done < <(git -C "$AB_REPO" rev-list HEAD -- "$pad" 2>/dev/null)
}

is_goed_sha() {   # $1 = sha1
    local s
    [ -n "$1" ] || return 1
    goede_shas_laden
    for s in "${AB_GOED_SHAS[@]}"; do [ "$s" = "$1" ] && return 0; done
    return 1
}

# Het kenmerk van een bestand: sha1, inode en mtime (op de nanoseconde). Een bestand dat wij hebben
# neergezet heeft precies dit kenmerk in het grootboek; een DLL die iemand daarna neerzette (ook een
# byte-gelijke) heeft een ander inode of een andere mtime.
kenmerk_van() {   # $1 = bestand
    [ -f "$1" ] || return 1
    printf '%s %s %s\n' "$(sha_van "$1")" "$(stat -c %i "$1")" "$(date -r "$1" +%s.%N)"
}

# Het grootboek (AB_STAAT/geplaatst.<engine|view>): een regel per proef-DLL die een run in ENG of VIEW
# zette, zolang er een reservekopie is. Een DLL die erin staat is een achtergebleven proef-DLL, welke
# set varianten de volgende run ook heeft (herreview 2: eerst keek alleen AB_PROEF_SHAS, en dan bewaarde
# een run met een andere NEW de oude proef-DLL als origineel).
in_grootboek() {   # $1 = naam, $2 = bestand
    local k
    k=$(kenmerk_van "$2") || return 1
    [ -f "$AB_STAAT/geplaatst.$1" ] && grep -qxF "$k" "$AB_STAAT/geplaatst.$1"
}

# Een proef-DLL neerzetten: eerst naast het doel kopieren, het kenmerk van DAT bestand in het grootboek
# schrijven, en dan pas met mv op zijn plek (mv houdt inode en mtime). Zo staat een proef-DLL nooit in
# ENG of VIEW zonder regel in het grootboek, ook niet na kill -9.
plaats_proef() {   # $1 = bron, $2 = doel, $3 = engine | view
    local tmp="${2:?}.ab-nieuw.$$" k
    if cp -p "$1" "$tmp" && k=$(kenmerk_van "$tmp") && printf '%s\n' "$k" >> "$AB_STAAT/geplaatst.$3" &&
        mv -f "$tmp" "$2"; then
        return 0
    fi
    rm -f "${tmp:?}"
    return 1
}

# De DLL's opzij zetten in AB_STAAT/reserve: <naam>.dll met de sha1 ernaast (<naam>.dll.sha1), of
# <naam>.geen als er geen DLL was. Beide via een tijdelijk bestand en mv, de sha1 als laatste: een
# reservekopie zonder kloppende sha1 is half geschreven.
#
# Staat er nog een reservekopie (een vorige run is met kill -9 gestopt), dan geldt die ALLEEN als hij
# heel is en de huidige DLL precies de proef-DLL is die een run neerzette (kenmerk in het grootboek).
# Anders is hij oud -- de engine is daarna bijgewerkt, ook als dat byte-gelijk aan een variant gebeurde --
# en gaan reservekopie en grootboek weg, met een waarschuwing. Daarna wordt de huidige DLL bewaard als
# origineel, behalve als dat aantoonbaar niet kan: hij staat in het grootboek (een proef-DLL), of hij is
# een variant van deze run, en in beide gevallen ook geen vastgelegde oplevering uit git. Dan weigeren.
reserve_maak() {
    AB_RESERVE="${AB_STAAT:?}/reserve"
    mkdir -p "$AB_RESERVE"
    local naam bron res huidig bewaard geldig proef
    local -a kopieer=()
    for naam in engine view; do
        if [ "$naam" = engine ]; then bron="$ENG"; else bron="$VIEW"; fi
        res="$AB_RESERVE/$naam.dll"
        huidig=""
        [ -f "$bron" ] && huidig=$(sha_van "$bron")
        proef=nee
        in_grootboek "$naam" "$bron" && proef=ja
        if [ -e "$res" ] || [ -e "$res.sha1" ] || [ -e "$AB_RESERVE/$naam.geen" ]; then
            geldig=nee
            if [ "$proef" = ja ]; then
                if [ -e "$AB_RESERVE/$naam.geen" ] && [ ! -e "$res" ]; then
                    geldig=ja
                elif [ -f "$res" ] && [ -f "$res.sha1" ]; then
                    bewaard=$(cat "$res.sha1")
                    [ -n "$bewaard" ] && [ "$bewaard" = "$(sha_van "$res")" ] && geldig=ja
                fi
            fi
            if [ "$geldig" = ja ]; then
                echo "reservekopie ($naam) uit een afgebroken run gevonden en heel, en de huidige $naam-DLL is de proef-DLL die die run neerzette; de reservekopie wordt straks teruggezet"
                continue
            fi
            echo "waarschuwing: oude reservekopie ($naam) in $AB_RESERVE weggegooid (de huidige DLL ${huidig:-(afwezig)} is niet de proef-DLL die een run neerzette, of de kopie is niet heel)" >&2
            rm -f "${res:?}" "${res:?}.sha1" "${AB_RESERVE:?}/${naam:?}.geen"
        fi
        if [ -n "$huidig" ] && ! is_goed_sha "$huidig" && { [ "$proef" = ja ] || is_proef_sha "$huidig"; }; then
            echo "geweigerd: de huidige $naam-DLL ($bron, sha1 $huidig) is een proef-DLL ($([ "$proef" = ja ] && echo "neergezet door een eerdere run" || echo "een variant van deze run")) en geen vastgelegde oplevering, en er is geen geldige reservekopie: het origineel is onbekend. Oplossing: zet de vastgelegde DLL terug, bijvoorbeeld: git -C $AB_REPO show HEAD:patches/RobinHood/dropin/rg-rhio.dll > $bron" >&2
            exit 3
        fi
        rm -f "${AB_STAAT:?}/geplaatst.${naam:?}"   # nieuw origineel, nieuw grootboek
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
                rm -f "${AB_RESERVE:?}/${naam:?}.dll" "${AB_RESERVE:?}/${naam:?}.dll.sha1" "${AB_STAAT:?}/geplaatst.${naam:?}"
            else
                echo "FOUT: kon $bron niet terugzetten; de reservekopie blijft in $AB_RESERVE/$naam.dll" >&2
                mis=1
            fi
        elif [ -e "$AB_RESERVE/$naam.geen" ]; then
            if rm -f "${bron:?}"; then
                rm -f "${AB_RESERVE:?}/${naam:?}.geen" "${AB_STAAT:?}/geplaatst.${naam:?}"
            else
                mis=1
            fi
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
