#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# RavenGun in één map (docs/portable.md), zoals Dolphin in portabele modus.
#
# Alles wat de programma's nodig hebben staat in de map en wordt relatief aan de
# binary gevonden:
#   <map>/bin                      de binaries (ravengun, ravengunctl, ravengund, rg-direct, ...)
#   <map>/lib                      de gedeelde bibliotheken (Qt, lib32)
#   <map>/share/ravengun           de datamap: patches/ (profielen en drop-ins), packaging/, plugins/, docs/
#   <map>/User                     per-gebruiker: config/, state/, cache/ en share/ (wine, prefixen, openparrot, ...)
#   <map>/ravengun.sh              de starter die de omgeving op de map zet en een binary kiest
#
# De map is zelfstandig: de runtime (wine/proton, de prefixen, de OpenParrot-build),
# de staat van de games (views, schrijflagen, SRAM/regiogegevens, HASP-antwoorden) en
# de configuratie worden ECHT gekopieerd, niet gekoppeld. Alle koppelingen die naar
# ~/.local wezen worden daarna herschreven naar een relatieve koppeling binnen de map
# (of uitgepakt als dat niet kan). Na het kopiëren heeft ~/.local niets meer nodig;
# alleen de spelinstallaties (de NAS) blijven buiten de map. Blijft er na afloop toch
# een koppeling naar ~/.local over, of wijst een relatieve koppeling uit de map, dan
# stopt het script met een foutcode.
#
# Wat naar ~/.local wijst en niet mee gekopieerd is, wordt UITGEPAKT (de inhoud komt in de map). Alleen
# een mapkoppeling naar een bekende cache op de toegestane lijst (standaard de Steam-appcache,
# ~600 MB, die de eigenaar zelf weer vult) wordt een lege map; elke andere mapkoppeling wordt echt
# gekopieerd of het script stopt. De profielen komen uit `git archive HEAD patches`, niet uit de werkmap.
# De overlay-toestand van de schrijflagen (whiteouts als char-device 0:0, user.overlay.*-xattrs) wordt
# meegenomen en gecontroleerd (scripts/portable_tools.py).
#
# User/ wordt in User.new opgebouwd en pas na alle controles op zijn plek gezet (een
# rename). Een mislukte of onderbroken run laat dus nooit een halve User/ achter en
# een bestaande User/ blijft bij een fout ongemoeid. De wissel gebeurt niet zolang een
# proces User/ gebruikt (mount, werkmap, open bestand; portable_tools.py busy). De oude
# User/ blijft daarna als User.old staan: een gewone verversing laat hem staan (met een
# regel); alleen de volgende --reset-user of --drop-old gooit hem weg, aan het begin (na de
# controles op rsync en ruimte, waarin zijn ruimte meetelt) en alleen als niets hem gebruikt.
# Tijdens de wissel staat het merk .user-swap; alleen dan zet een volgende run een User.old
# terug als User/ ontbreekt (anders wordt User/ gewoon opnieuw opgebouwd). bin/, lib/ en share/ worden als <naam>.new
# gebouwd en via <naam>.old gewisseld. Eén run tegelijk per map (flock op .make-portable.lock).
# De rechten die het script in de bron tijdelijk opent, staan in .portable-perms.journal in de
# map; een volgende run zet ze terug als een run hard afgebroken werd (SIGKILL, stroom weg).
#
# Gebruik: scripts/make-portable.sh [--reset-user] [--drop-old] [DOEL]
#   --reset-user  bouw heel User/ opnieuw op (anders blijft User/ staan en verversen
#                 we alleen bin/, lib/ en share/ravengun/patches). Een User.old van de
#                 vorige keer gaat eerst weg (telt dus als vrije ruimte); de huidige User/
#                 blijft tot het eind staan en daarna als User.old: ruimte voor twee kopieën.
#   --drop-old    gooi User.old weg (als niets hem gebruikt), ook bij een gewone verversing.
#   DOEL          de map om te vullen (standaard dist/RavenGun); wordt aangemaakt. Hij moet
#                 nieuw of leeg zijn, of al een portabele map (.ravengun-portable, of een
#                 ravengun.sh met de kopregel van de starter), en mag niet de thuismap, de
#                 runtime, de installatie, de configuratie of de repo zijn (of een ouder
#                 daarvan), en er ook niet in liggen (behalve onder <repo>/dist).
# Voor de test (tests/make_portable_test.sh) overschrijfbaar via de omgeving:
#   RG_PORTABLE_INSTALL, RG_PORTABLE_RUNTIME, RG_PORTABLE_CONFIG, RG_PORTABLE_TUI_PROTON,
#   RG_PORTABLE_PATCHES (een patches-map om te leveren in plaats van git archive HEAD),
#   RG_PORTABLE_EXTRA_FREE (bytes vrije ruimte bovenop de runtime),
#   RG_PORTABLE_EMPTY_OK (doelen, met * als joker, gescheiden door ":", die als lege map mogen),
#   RG_PORTABLE_MAX_EXTRACT (bytes; grootste mapkoppeling die nog uitgepakt wordt).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST=""
RESET_USER=0
DROP_OLD=0
# De hele kop van dit script (alle commentaarregels vanaf regel 2 tot de eerste regel code).
usage() {
    awk 'NR == 1 { next } /^#/ { sub(/^# ?/, ""); print; next } { exit }' "$0"
}
while [[ $# -gt 0 ]]; do
    case "$1" in
        --reset-user) RESET_USER=1; shift ;;
        --drop-old) DROP_OLD=1; shift ;;
        -h|--help) usage; exit 0 ;;
        -*) echo "make-portable: onbekende optie: $1" >&2; usage >&2; exit 2 ;;
        *) [[ -z "$DEST" ]] || { echo "make-portable: meer dan één DOEL: $DEST en $1" >&2; usage >&2; exit 2; }
           DEST="$1"; shift ;;
    esac
done
DEST="${DEST:-$ROOT/dist/RavenGun}"
# Absoluut, anders belanden relatieve paden in library.json.
DEST="$(realpath -m -- "$DEST")"
INSTALL="${RG_PORTABLE_INSTALL:-$HOME/.local/opt/ravengun}"
RUNTIME="${RG_PORTABLE_RUNTIME:-$HOME/.local/share/ravengun}"
CONFIG="${RG_PORTABLE_CONFIG:-$HOME/.config/ravengun}"
# De prefixen in de runtime linken naar de Proton-build van de app; die staat buiten de
# runtime maar is identiek aan $RUNTIME/proton (gecontroleerd 2026-10-18, zelfde md5).
TUI_PROTON="${RG_PORTABLE_TUI_PROTON:-$HOME/.local/share/TeknoParrotUI/proton}"
PATCHES="${RG_PORTABLE_PATCHES:-}"   # leeg: de gecommitte profielen uit git (HEAD)
EXTRA_FREE="${RG_PORTABLE_EXTRA_FREE:-1073741824}"
MAX_EXTRACT="${RG_PORTABLE_MAX_EXTRACT:-2147483648}"
IFS=: read -r -a EMPTY_OK <<< "${RG_PORTABLE_EMPTY_OK:-$HOME/.local/share/Steam/appcache}"
TOOLS="$ROOT/scripts/portable_tools.py"

USERDIR="$DEST/User"
PATCH_TMP="$DEST/.patches-export"   # tijdelijke git-archive van patches/ (in de doelmap, nooit in /tmp)
NEWUSER="$DEST/User.new"
OLDUSER="$DEST/User.old"
ACTUAL_USER="$USERDIR"   # waar User/ nu echt staat: tijdens de opbouw User.new
BUILDING=0               # 1 zolang User.new onderweg is (dan ruimt de EXIT-val hem op)
BUILD_USER=0             # 1 als User/ in deze run opnieuw opgebouwd wordt

# Wat "de thuismap-indeling" is: hier mag na afloop niets meer naar wijzen.
# ~/.steam (root, sdk32, bin32, ...) en ~/.steampath zijn koppelingen naar ~/.local/share/Steam.
HOME_ROOTS=("$HOME/.local" "$HOME/.config/ravengun" "$HOME/.steam" "$HOME/.steampath"
            "$RUNTIME" "$INSTALL" "$CONFIG" "$TUI_PROTON" "$ROOT")
MARK="$DEST/.ravengun-portable"   # deze map is van make-portable
STARTER_LINE="# Starter van de portabele RavenGun-map"   # kopregel van ravengun.sh (write_launcher)
LOCK="$DEST/.make-portable.lock"     # één run tegelijk per map
JOURNAL="$DEST/.portable-perms.journal"   # rechten die fix_perms read in de bron opende (restore_src_perms)
SWAPMARK="$DEST/.user-swap"          # staat er alleen tijdens de wissel van User/ (tussen de twee renames)
LAUNCHER_TMP="$DEST/.ravengun.sh.new"     # tijdelijke starter (write_launcher)

die() { echo "make-portable: $*" >&2; exit 1; }

# DOEL mag nooit de thuismap, de runtime, de installatie, de configuratie, de Proton-build of de repo zijn, of een ouder
# daarvan (een run gooit in DOEL bin/, lib/, share/ en User* weg: make-portable.sh ~/.local wiste ~/.local/{bin,lib,share}),
# en ook niet erin liggen, behalve onder <repo>/dist (de thuismap zelf mag wel, ~/RavenGun bijvoorbeeld). Op echte paden.
guard_dest() {
    local r rr root_real
    [[ "$DEST" != / ]] || die "doelmap / weigert het script"
    root_real="$(realpath -m -- "$ROOT")"
    for r in "$HOME" "$RUNTIME" "$INSTALL" "$CONFIG" "$TUI_PROTON" "$ROOT"; do
        [[ -n "$r" ]] || continue
        rr="$(realpath -m -- "$r")"
        if [[ "$rr" == "$DEST" || "$rr" == "$DEST"/* ]]; then
            die "doelmap $DEST is $r of een map erboven; daar wordt bin/, lib/ en share/ weggegooid. Kies een eigen map, bijvoorbeeld $ROOT/dist/RavenGun"
        fi
        [[ "$r" == "$HOME" ]] && continue
        if [[ "$DEST" == "$rr"/* ]]; then
            [[ "$rr" == "$root_real" && "$DEST" == "$root_real/dist/"* ]] && continue
            die "doelmap $DEST ligt in $r; kies een eigen map, bijvoorbeeld $ROOT/dist/RavenGun"
        fi
    done
    if [[ -e "$DEST" ]]; then
        [[ -d "$DEST" ]] || die "doelmap $DEST bestaat en is geen map"
        # Bewijs: het merkbestand, of een ravengun.sh die echt onze starter is (de kopregel), niet elk bestand met die naam.
        if [[ -n "$(ls -A -- "$DEST" 2>/dev/null)" && ! -e "$MARK" ]] &&
           ! [[ -f "$DEST/ravengun.sh" && "$(head -n 5 -- "$DEST/ravengun.sh" 2>/dev/null)" == *"$STARTER_LINE"* ]]; then
            die "doelmap $DEST is niet leeg en is geen portabele RavenGun-map (geen .ravengun-portable en geen ravengun.sh van de starter); kies een nieuwe of lege map"
        fi
    fi
}
guard_dest

if [[ ! -x "$INSTALL/bin/ravengund" ]]; then
    die "$INSTALL/bin/ravengund ontbreekt; bouw en installeer eerst (scripts/build-app.sh)"
fi

# Onleesbare overlay-werkmapjes (mode 000, van bwrap; ze ontstaan ook in $DEST/User als
# een game vanuit de map draait) weer toegankelijk maken, anders mislukken kopiëren en
# opruimen. Een geneste mode-000-map ziet find pas als de buitenste leesbaar is, dus
# herhalen tot er niets meer te herstellen valt.
#   fix_perms read  PAD...  alleen lezen nodig (de bron): bestanden u+r, mappen u+rx. Nooit u+w:
#                           Wine vertaalt een ontbrekend schrijfbit naar "alleen lezen", dus een
#                           chmod u+w op een live prefix verandert de game stilletjes. Vóór elke chmod komt
#                           (inode, oude rechten, nieuwe rechten, pad) in het journaal $JOURNAL in de map;
#                           restore_src_perms zet ze na het kopiëren terug (een mode-000-werkmap van een
#                           overlay blijft zo echt 000, niet 500), en een volgende run doet dat als deze
#                           hard afgebroken werd.
#   fix_perms write PAD...  de boom wordt weggegooid of is van ons: u+rwX.
fix_perms() {
    local mode="$1" d i k m n
    local -a bd bf paths
    shift
    for d in "$@"; do
        [[ -e "$d" ]] || continue
        for i in 1 2 3 4 5 6 7 8 9 10; do
            bd=(); bf=()
            if [[ "$mode" == write ]]; then
                mapfile -d '' -t bd < <(find "$d" -type d ! -perm -u+rwx -print0 2>/dev/null || true)
                mapfile -d '' -t bf < <(find "$d" -type f ! -perm -u+rw -print0 2>/dev/null || true)
                (( ${#bd[@]} + ${#bf[@]} )) || break
                (( ${#bd[@]} )) && { chmod u+rwx -- "${bd[@]}" 2>/dev/null || true; }
                (( ${#bf[@]} )) && { chmod u+rw -- "${bf[@]}" 2>/dev/null || true; }
            else
                # (soort, inode, rechten, pad): eerst in het journaal, dan u+rX (mappen u+rx, bestanden u+r, en u+x
                # als iemand x heeft: precies wat chmod u+rX doet, zodat restore de nieuwe rechten kan herkennen)
                mapfile -d '' -t bd < <(find "$d" \( \( -type d ! -perm -u+rx \) -o \( -type f ! -perm -u+r \) \) -printf '%y\0%i\0%m\0%p\0' 2>/dev/null || true)
                (( ${#bd[@]} )) || break
                paths=()
                # kopregel (zelfde vorm als een regel: 4 velden): voor welke bron dit journaal is
                [[ -s "$JOURNAL" ]] || printf '%s\0%s\0%s\0%s\0' "#bron" 1 - "$(realpath -m -- "$RUNTIME")" > "$JOURNAL"
                for ((k = 0; k + 3 < ${#bd[@]}; k += 4)); do
                    m=$((8#${bd[k + 2]}))
                    if [[ "${bd[k]}" == d ]] || (( m & 8#111 )); then n=$((m | 8#500)); else n=$((m | 8#400)); fi
                    printf '%s\0%o\0%o\0%s\0' "${bd[k + 1]}" "$m" "$n" "${bd[k + 3]}" >> "$JOURNAL"
                    paths+=("${bd[k + 3]}")
                done
                sync -- "$JOURNAL" 2>/dev/null || true
                chmod u+rX -- "${paths[@]}" 2>/dev/null || true
            fi
        done
    done
    return 0
}

# De rechten uit het journaal terugzetten, binnenste eerst (in omgekeerde volgorde: een buitenste map die weer 000 wordt, zou
# de binnenste onbereikbaar maken). Alleen als het pad nog hetzelfde inode heeft en nog precies de rechten die wij zetten; is er
# intussen iets veranderd (een game, de gebruiker), dan blijft het zoals het is, met een melding. Daarna weg met het journaal.
# Het journaal is zelf niet te vertrouwen (een afgekapte laatste regel na SIGKILL, een map die met journaal en al gekopieerd is):
# alleen hele regels van 4 velden, met getallen waar getallen horen, elk (inode, pad) één keer (de eerste regel heeft de echte
# oude rechten), en alleen als de kopregel dezelfde bron noemt en het pad in die bron ligt. Breekt nooit de run af.
restore_src_perms() {
    local k n cur key skipped=0 rt runtime_real
    local -a e
    local -A first=()
    [[ -f "$JOURNAL" ]] || return 0
    mapfile -d '' -t e < "$JOURNAL" || true
    n=$(( ${#e[@]} / 4 * 4 ))           # een afgekapte laatste regel telt niet mee
    runtime_real="$(realpath -m -- "$RUNTIME")"
    if (( n < 4 )) || [[ "${e[0]}" != "#bron" ]]; then
        (( n == 0 )) || echo "make-portable: let op: $JOURNAL heeft geen kopregel; niets teruggezet" >&2
        rm -f -- "$JOURNAL"; return 0
    fi
    rt="${e[3]}"
    if [[ "$rt" != "$runtime_real" ]]; then
        echo "make-portable: let op: $JOURNAL hoort bij een andere bron ($rt, niet $runtime_real); niets teruggezet" >&2
        rm -f -- "$JOURNAL"; return 0
    fi
    for ((k = 4; k < n; k += 4)); do
        key="${e[k]}/${e[k + 3]}"
        [[ -n "${first[$key]+x}" ]] || first[$key]=$k
    done
    for ((k = n - 4; k >= 4; k -= 4)); do
        key="${e[k]}/${e[k + 3]}"
        [[ "${first[$key]}" == "$k" ]] || continue     # dubbele regel: alleen de eerste telt
        if ! [[ "${e[k]}" =~ ^[0-9]+$ && "${e[k + 1]}" =~ ^[0-7]{1,4}$ && "${e[k + 2]}" =~ ^[0-7]{1,4}$ ]] ||
           ! [[ "${e[k + 3]}" == "$RUNTIME"/* || "${e[k + 3]}" == "$RUNTIME" || "${e[k + 3]}" == "$rt"/* || "${e[k + 3]}" == "$rt" ]]; then
            skipped=$((skipped + 1))
            (( skipped <= 10 )) && echo "make-portable: let op: ongeldige regel in $JOURNAL overgeslagen (${e[k + 3]})" >&2
            continue
        fi
        cur="$(stat -c '%i %a' -- "${e[k + 3]}" 2>/dev/null || true)"
        if [[ "$cur" =~ ^([0-9]+)\ ([0-7]+)$ && "${BASH_REMATCH[1]}" == "${e[k]}" ]] && (( 8#${BASH_REMATCH[2]} == 8#${e[k + 2]} )); then
            chmod -- "${e[k + 1]}" "${e[k + 3]}" 2>/dev/null || true
        else
            skipped=$((skipped + 1))
            (( skipped <= 10 )) && echo "make-portable: let op: rechten van ${e[k + 3]} niet teruggezet (verdwenen of intussen veranderd)" >&2
        fi
    done
    rm -f -- "$JOURNAL"
}

# Een boom weggooien, nadat de rechten hersteld zijn (anders faalt rm onder set -e en
# blijft er 19 GB achter). Dit zijn bomen van ons (User.new/.old, bin.new, ...): schrijfrechten mogen.
remove_tree() {
    local d
    for d in "$@"; do
        [[ -e "$d" || -L "$d" ]] || continue
        fix_perms write "$d"
        rm -rf -- "$d"
    done
}

# bin/, lib/ en share/: een onderbroken wissel herstellen. Ontbreekt <naam>, dan komt <naam>.old terug; <naam>.new gaat alleen
# weg als <naam> er (weer) staat, en een overgebleven <naam>.old ook. Bij het begin van een run en in de EXIT-val.
recover_app_dirs() {
    local x
    for x in "$DEST/bin" "$DEST/lib" "$DEST/share"; do
        if [[ ! -e "$x" && ! -L "$x" && -d "$x.old" ]]; then
            echo "make-portable: $(basename "$x")/ ontbrak; $(basename "$x").old teruggezet" >&2
            mv -T -- "$x.old" "$x" || continue
        fi
        if [[ -e "$x" || -L "$x" ]]; then
            remove_tree "$x.new" "$x.old"
        fi
    done
}

# <naam>.new op de plek van <naam>: eerst <naam> opzij (<naam>.old), dan .new erin, dan pas .old weg. Een onderbreking laat zo
# nooit een halve <naam> achter; de twee renames worden niet door Ctrl-C onderbroken.
swap_in() {
    local x="$DEST/$1"
    remove_tree "$x.old"
    trap '' INT TERM
    if [[ -e "$x" || -L "$x" ]]; then
        mv -T -- "$x" "$x.old" || { trap 'exit 130' INT; trap 'exit 143' TERM; die "$1/ opzij zetten mislukt"; }
    fi
    if ! mv -T -- "$x.new" "$x"; then
        [[ -d "$x.old" && ! -e "$x" ]] && mv -T -- "$x.old" "$x"
        trap 'exit 130' INT; trap 'exit 143' TERM
        die "$1.new op zijn plek zetten mislukt; de vorige $1/ is teruggezet"
    fi
    trap 'exit 130' INT; trap 'exit 143' TERM
    remove_tree "$x.old"
}

# Bij elke afloop die niet geslaagd is (fout, Ctrl-C, set -e) de halve User.new opruimen; een halve wissel van bin/lib/share
# herstellen. Alleen als deze run de map vastheeft (anders is het de User.new van een andere run).
cleanup() {
    local rc=$?
    if [[ "$LOCKED" == 1 ]]; then
        restore_src_perms || true
        if [[ "$BUILDING" == 1 ]]; then
            echo "make-portable: niet afgemaakt; $NEWUSER wordt opgeruimd (een bestaande User/ blijft staan)" >&2
            remove_tree "$NEWUSER" || true
        fi
        remove_tree "$PATCH_TMP" || true
        rm -f -- "$LAUNCHER_TMP" || true
        recover_app_dirs || true
    fi
    exit "$rc"
}
LOCKED=0
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# 1. De applicatie zelf, als vervanging (niet samenvoegen): bin/, lib/ en share/ worden elk
# eerst als <naam>.new opgebouwd en dan in één keer op hun plek gezet; een onderbroken run laat
# dus nooit een map zonder share/ (of met een halve) achter. De profielen komen uit de GECOMMITTE repo (git archive HEAD
# patches, zoals tools/dev/make-release.sh), niet uit de werkmap en niet uit de installatie
# (die legt share/ravengun/patches als koppeling naar ../../patches neer): zo komen geen
# ongevolgde of onafgemaakte bestanden in de map. Daarna alleen de levering
# (tools/dev/ship-patches.py): geen dossiers.
refresh_app() {
    local d src="$PATCHES"
    for d in bin lib; do
        [[ -e "$INSTALL/$d" ]] || continue
        remove_tree "$DEST/$d.new"
        cp -a "$INSTALL/$d" "$DEST/$d.new"
        swap_in "$d"
    done
    remove_tree "$DEST/share.new"
    if [[ -e "$INSTALL/share" ]]; then
        cp -a "$INSTALL/share" "$DEST/share.new"
    elif [[ -e "$DEST/share" ]]; then
        cp -a "$DEST/share" "$DEST/share.new"
    else
        mkdir -p "$DEST/share.new"
    fi
    # Het merkbestand van cmake --install (cmake/PatchesLayout.cmake) hoort bij een installatie, niet in de map.
    rm -f -- "$DEST/share.new/ravengun/.patches-installatie"
    if [[ -z "$src" ]]; then
        git -C "$ROOT" rev-parse --verify -q HEAD >/dev/null 2>&1 ||
            die "$ROOT is geen git-checkout met commits; de profielen komen uit HEAD (of zet RG_PORTABLE_PATCHES)"
        if [[ -n "$(git -C "$ROOT" status --porcelain -- patches 2>/dev/null)" ]]; then
            echo "make-portable: let op: patches/ heeft niet-gecommitte wijzigingen of ongevolgde bestanden; alleen HEAD gaat mee" >&2
        fi
        remove_tree "$PATCH_TMP"
        mkdir -p "$PATCH_TMP"
        git -C "$ROOT" archive --format=tar HEAD patches | tar -x -C "$PATCH_TMP" --strip-components=1
        src="$PATCH_TMP"
    fi
    if [[ -d "$src" ]]; then
        remove_tree "$DEST/share.new/ravengun/patches"
        mkdir -p "$DEST/share.new/ravengun"
        python3 -I "$ROOT/tools/dev/ship-patches.py" "$src" "$DEST/share.new/ravengun/patches"
    fi
    remove_tree "$PATCH_TMP"
    swap_in share
}

# 2. Voor het kopiëren van de runtime: rsync aanwezig en genoeg ruimte? Dit gebeurt
# voordat er iets aangeraakt is (ook vóór het weggooien van User.old), zodat een afbreker niets achterlaat.
# Argument: 1 als User.old straks weggegooid wordt; zijn ruimte telt dan mee (alleen gemeten als de vrije ruimte alleen niet
# genoeg is: du over 19 GB kost tijd).
preflight_user() {
    local need avail old=0 drop="${1:-0}"
    command -v rsync >/dev/null || die "rsync ontbreekt (nodig om de runtime te kopiëren); User.old en User/ blijven staan"
    fix_perms read "$RUNTIME"   # du en rsync mogen niet struikelen over de mode-000 werkmapjes (alleen lees-/zoekrechten)
    need="$(du -sb --exclude=bench-captures --exclude=halo-overlay "$RUNTIME" 2>/dev/null | cut -f1 || true)"
    avail="$(df -B1 --output=avail "$DEST" 2>/dev/null | tail -1 || true)"
    [[ "$need" =~ ^[0-9]+$ && "$avail" =~ ^[0-9]+$ ]] || return 0
    if [[ "$drop" == 1 && "$avail" -lt $((need + EXTRA_FREE)) && -d "$OLDUSER" ]]; then
        old="$(du -sb -- "$OLDUSER" 2>/dev/null | cut -f1 || true)"
        [[ "$old" =~ ^[0-9]+$ ]] || old=0
    fi
    if [[ $((avail + old)) -lt $((need + EXTRA_FREE)) ]]; then
        echo "make-portable: te weinig ruimte in $DEST: nodig ~$((need / 1048576)) MB, vrij $((avail / 1048576)) MB" \
             "(plus $((old / 1048576)) MB van User.old); er is niets weggegooid" >&2
        if [[ -d "$USERDIR" ]]; then
            echo "make-portable: de oude User/ blijft tot het eind staan; gooi hem zelf weg als je hem niet meer nodig hebt en draai opnieuw" >&2
        fi
        exit 1
    fi
}

# rsync-afloop beoordelen: 0 is goed, 24 (bronbestanden verdwenen tijdens het kopiëren,
# bij een draaiende game normaal) ook; al het andere is een mislukte kopie.
rsync_ok() {
    [[ "$1" == 0 || "$1" == 24 ]]
}

# 3. De runtime en de staat van de games kopiëren, in de nieuwe User.new. Elke fout
# stopt het script; de EXIT-val ruimt User.new op en de bestaande User/ blijft heel.
copy_runtime() {
    local dst="$NEWUSER/share/ravengun" path name rc globs
    local -a entries
    mkdir -p "$dst"
    # Ook verborgen mappen (dotglob); een lege runtime geeft geen letterlijke "*" (nullglob). Daarna de oude instellingen terug.
    globs="$(shopt -p dotglob nullglob || true)"
    shopt -s dotglob nullglob
    entries=("$RUNTIME"/*)
    eval "$globs"
    for path in "${entries[@]}"; do
        [[ -e "$path" || -L "$path" ]] || continue
        name="$(basename "$path")"
        case "$name" in bench-captures|halo-overlay) continue ;; esac
        rc=0
        # -X: de user.overlay.*-xattrs van de schrijflagen. Zonder rootrechten slaat rsync char-devices
        # over (de whiteouts); portable_tools.py whiteouts maakt ze na en controleert ze per pad, alleen
        # waar de overlays leven (--scope auto). Bij code 24 (een draaiende game) worden verschillen
        # waarschuwingen; wat na de rsync in de bron ontstond wordt gemeld, niet vergeleken.
        rsync -aX --info=nonreg0 "$path" "$dst/" || rc=$?
        rsync_ok "$rc" || die "$name kopiëren mislukt (rsync-code $rc); de bestaande map blijft ongemoeid"
        if [[ "$rc" == 24 ]]; then
            echo "make-portable: let op: tijdens het kopiëren van $name verdwenen bestanden uit de bron (rsync-code 24, een draaiende game?); die ontbreken in de kopie" >&2
        fi
        python3 -I "$TOOLS" whiteouts --scope auto --rsync-rc "$rc" "$path" "$dst/$name" ||
            die "de overlay-toestand (whiteouts/xattrs) van $name is niet te reproduceren; de bestaande map blijft ongemoeid"
        echo "  gekopieerd: $name"
    done
}

# 4. De configuratie van deze gebruiker, zonder de geheimen en de back-ups: web-token is
# een sessiesleutel en de backups/*.bak zijn oude kopieën.
copy_config() {
    local d="$NEWUSER/config/ravengun" rc=0
    mkdir -p "$d"
    [[ -d "$CONFIG" ]] || return 0
    rsync -a --exclude web-token --exclude backups --exclude '*.bak' "$CONFIG/" "$d/" || rc=$?
    rsync_ok "$rc" || die "de configuratie kopiëren mislukt (rsync-code $rc)"
    echo "  config: $CONFIG -> User/config/ravengun (zonder web-token en back-ups)"
}

# 5. Alle koppelingen in de nieuwe boom in één doorgang herschrijven en controleren
# (scripts/portable_tools.py links). Tijdens de opbouw staat User/ nog als User.new; de
# relatieve koppelingen moeten kloppen voor de plek waar hij straks staat, dus rekent het
# met het eindpad en kijkt het op schijf op het huidige pad. Gescand wordt alleen wat in
# deze run gebouwd wordt (bin/, lib/, share/ en de nieuwe User); een oude User/ blijft
# ongemoeid. Exit 1 bij een koppeling die uit de map wijst, naar ~/.local blijft wijzen
# of niet uitgepakt kan worden.
relink() {
    local r
    local -a args=(links --dest "$DEST" --actual-user "$ACTUAL_USER" --user "$USERDIR" --scan-user "$BUILD_USER"
                   --runtime "$RUNTIME" --install "$INSTALL" --config "$CONFIG" --tui-proton "$TUI_PROTON"
                   --max-extract "$MAX_EXTRACT")
    for r in "${HOME_ROOTS[@]}"; do args+=(--home-root "$r"); done
    for r in "${EMPTY_OK[@]}"; do [[ -n "$r" ]] && args+=(--empty-ok "$r"); done
    python3 -I "$TOOLS" "${args[@]}"
}

# 6. De wrappers van de Wine-route pakken hun runtime standaard uit
# ~/.local/share/ravengun; in de map horen ze dat uit de map zelf te doen (de map
# boven de map van het script). Draait NA relink: een wrapper die een koppeling naar een
# script in de repo was (wrappers/wine-prefix -> ~/projects/ravengun/scripts/wine-prefix.sh)
# is dan een echt bestand in de map. Er wordt nooit door een koppeling heen geschreven: een
# koppeling wordt alleen gevolgd als haar echte doel een gewoon bestand IN de map is, en elk
# bestand wordt vervangen via een tijdelijk bestand + rename (nooit open(..., 'w') op het pad).
fix_scripts() {
    python3 -I - "$ACTUAL_USER" "$DEST" <<'PYEOF'
import os, stat, sys
actual_user, dest = sys.argv[1], sys.argv[2]
real_dest = os.path.realpath(dest)
old = '$HOME/.local/share/ravengun'
new = '$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)'
root = os.path.join(actual_user, 'share/ravengun/wrappers')
done = set()
for dirpath, _dirs, files in os.walk(root):     # os.walk volgt geen mapkoppelingen onder root
    for name in files:
        p = os.path.join(dirpath, name)
        target = os.path.realpath(p)
        if not target.startswith(real_dest + '/'):
            print('  let op: wrapper {} wijst uit de map ({}); niet aangepast'.format(p, target), file=sys.stderr)
            continue
        try:
            st = os.lstat(target)
        except OSError:
            continue
        if not stat.S_ISREG(st.st_mode) or target in done:
            continue
        done.add(target)
        try:
            with open(target, encoding='utf-8') as f:
                s = f.read()
        except Exception:
            continue
        if old not in s:
            continue
        d = os.path.dirname(target)
        dmode = None
        if not os.access(d, os.W_OK | os.X_OK):     # de kopie is van ons: tijdelijk schrijfbaar, daarna terug
            dmode = stat.S_IMODE(os.lstat(d).st_mode)
            os.chmod(d, dmode | 0o700)
        try:
            tmp = target + '.rgfix'
            with open(tmp, 'w', encoding='utf-8') as f:
                f.write(s.replace(old, new))
            os.chmod(tmp, stat.S_IMODE(st.st_mode))
            os.replace(tmp, target)
        finally:
            if dmode is not None:
                os.chmod(d, dmode)
        print('  wrapper: {} pakt de runtime nu uit de map'.format(target))
PYEOF
}

# De paden in de configuratie van de app (library.json, config.json) wijzen naar de oude
# plek (~/.local, of een eerdere plek van deze map). Het eerste deel van het pad tot en met de
# markering wordt vervangen door de map; wat ervoor staat (`env X=1 `) en erachter staat blijft.
# De markeringen: /User/share/ravengun/, /.local/share/ravengun/, /.config/ravengun/ en
# /User/config/ravengun/ (de configuratiemap van een verhuisde portabele map). Dezelfde code
# zit in de starter (ravengun.sh), die hem bij elke start draait zodat de map kan verhuizen.
# Een pad met een spatie erin vóór de markering wordt niet herkend.
# argumenten: <mapwortel> <map met library.json en config.json>
PATHFIX_PY="$(cat <<'PYEOF'
import json, os, re, sys
here, base = sys.argv[1], sys.argv[2]
ROOTS = {'/User/share/ravengun/': 'User/share/ravengun',
         '/.local/share/ravengun/': 'User/share/ravengun',
         '/.config/ravengun/': 'User/config/ravengun',
         '/User/config/ravengun/': 'User/config/ravengun'}
PAT = re.compile(r'(?<![^\s"\'=:,;(])(?:/[^\s"\'=:,;()]*?)?(?P<m>' + '|'.join(re.escape(m) for m in ROOTS) + ')')
def fix(s):
    return PAT.sub(lambda m: os.path.join(here, ROOTS[m.group('m')]) + '/', s)
def walk(o):
    if isinstance(o, dict):
        return {k: walk(v) for k, v in o.items()}
    if isinstance(o, list):
        return [walk(v) for v in o]
    return fix(o) if isinstance(o, str) else o
real_here = os.path.realpath(here)
for name in ('library.json', 'config.json'):
    p = os.path.join(base, name)
    # alleen een gewoon bestand in de map: nooit door een koppeling naar buiten heen schrijven
    if os.path.islink(p) or not os.path.isfile(p) or not os.path.realpath(p).startswith(real_here + '/'):
        continue
    try:
        with open(p, encoding='utf-8') as f:
            data = json.load(f)
    except Exception:
        continue
    out = walk(data)
    if out != data:
        text = json.dumps(out, ensure_ascii=False, indent=2) + '\n'
        tmp = p + '.rgfix'
        try:
            with open(tmp, 'w', encoding='utf-8') as f:
                f.write(text)
                f.flush()
                os.fsync(f.fileno())
            os.chmod(tmp, os.stat(p).st_mode & 0o7777)
            os.replace(tmp, p)
        except OSError as e:
            # nooit p zelf beschrijven (een volle schijf zou hem afkappen): p blijft zoals hij was
            try:
                if not os.path.isdir(tmp):
                    os.unlink(tmp)
            except OSError:
                pass
            print('ravengun: paden in {} niet bijgewerkt ({}); het bestand is ongewijzigd'.format(p, e.strerror or e),
                  file=sys.stderr)
            continue
        print('  paden: {} bijgewerkt'.format(p))
PYEOF
)"

# 7. De paden in de configuratie van de nieuwe User zetten op de map.
rewrite_config_paths() {
    python3 -I -c "$PATHFIX_PY" "$DEST" "$ACTUAL_USER/config/ravengun"
}

# 8. De starter: zet de omgeving op de map en kies een binary. Via een tijdelijk bestand en mv -T, nooit door een koppeling heen.
write_launcher() {
    local tmp="$LAUNCHER_TMP"
    rm -f -- "$tmp"
    {
        cat <<'EOF'
#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Starter van de portabele RavenGun-map. Zet alle paden op deze map en start het
# gevraagde programma: ravengun.sh [app|daemon|ctl|direct] [args...]
set -euo pipefail
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
export RAVENGUN_ROOT="$HERE"
export RAVENGUN_DATA_DIR="$HERE/share/ravengun"
export RAVENGUN_PATCHES_DIR="$HERE/share/ravengun/patches"
export RAVENGUN_USER_PATCHES_DIR="$HERE/User/share/ravengun/patches"
export RAVENGUN_HASP_DIR="${RAVENGUN_HASP_DIR:-$HERE/User/share/ravengun/hasp}"
# Alles wat de gebruiker maakt (configuratie, logboeken, views, wine-prefixen,
# HASP-antwoorden) blijft in User/.
export XDG_CONFIG_HOME="$HERE/User/config"
export XDG_STATE_HOME="$HERE/User/state"
export XDG_CACHE_HOME="$HERE/User/cache"
export XDG_DATA_HOME="$HERE/User/share"
export PATH="$HERE/bin:$PATH"
# Verhuisd? Zet de absolute paden in de configuratie weer op deze map.
python3 -I - "$HERE" "$HERE/User/config/ravengun" <<'PYEOF' >/dev/null || true
EOF
        printf '%s\n' "$PATHFIX_PY"
        cat <<'EOF'
PYEOF
mode="${1:-app}"; shift || true
case "$mode" in
    app)    exec "$HERE/bin/ravengun" "$@" ;;
    daemon) exec "$HERE/bin/ravengund" "$@" ;;
    ctl)    exec "$HERE/bin/ravengunctl" "$@" ;;
    direct) exec "$HERE/bin/rg-direct" "$@" ;;
    *)      exec "$HERE/bin/$mode" "$@" ;;
esac
EOF
    } > "$tmp"
    chmod 755 "$tmp"
    mv -T -- "$tmp" "$DEST/ravengun.sh"
}

# Is de map vrij: gebruikt geen proces (dat we kunnen lezen) hem? Een mount ervan of erin (ook als bind in een sandbox),
# werkmap, open of gemapt bestand telt als gebruik: dan exit 1, met de processen op stderr.
is_free() {
    python3 -I "$TOOLS" busy "$@"
}

# --- uitvoeren -----------------------------------------------------------------
# User.old weggooien als niets hem gebruikt. 0: weg (of er was er geen), 1: in gebruik (niets aangeraakt), 2: weggooien mislukt
# (de reden staat erboven). Draait in een ||-context (set -e staat daar uit), dus elke stap controleert zelf.
drop_old_user() {
    [[ -e "$OLDUSER" || -L "$OLDUSER" ]] || return 0
    is_free "$OLDUSER" || return 1
    echo "make-portable: User.old (de User/ van vóór de vorige --reset-user) wordt weggegooid"
    fix_perms write "$OLDUSER"
    rm -rf -- "$OLDUSER" || true
    if [[ -e "$OLDUSER" || -L "$OLDUSER" ]]; then
        echo "make-portable: User.old weggooien lukte niet helemaal (zie de melding van rm hierboven)" >&2
        return 2
    fi
    return 0
}

echo "make-portable: $INSTALL -> $DEST"
mkdir -p "$DEST"
[[ -e "$MARK" ]] || echo "map van scripts/make-portable.sh (docs/portable.md)" > "$MARK"
# Eén run tegelijk: een tweede run op dezelfde map zou de User.new van de eerste weggooien.
command -v flock >/dev/null || die "flock ontbreekt (util-linux)"
[[ -L "$LOCK" ]] && die "$LOCK is een koppeling; weg ermee en draai opnieuw"
exec 9>>"$LOCK"
flock -n 9 || die "er draait al een make-portable op $DEST (slot $LOCK); wacht tot die klaar is"
LOCKED=1

# Resten van een eerdere onderbroken run. Eerst de bronrechten uit een achtergebleven journaal (een hard afgebroken run), dan
# een halfgeruilde User/ terugzetten, een halve wissel van bin/lib/share herstellen en tijdelijke mappen weg.
if [[ -f "$JOURNAL" ]]; then
    echo "make-portable: een vorige run is afgebroken; de rechten in de bron uit $JOURNAL terugzetten"
    restore_src_perms
fi
# User.old komt alleen terug als het merk van een onderbroken wissel er staat; heeft de gebruiker User/ zelf weggegooid, dan
# wordt een nieuwe User/ opgebouwd en blijft User.old (de saves van vóór de vorige reset) gewoon staan.
if [[ -e "$SWAPMARK" ]]; then
    if [[ ! -e "$USERDIR" && ! -L "$USERDIR" && -d "$OLDUSER" ]]; then
        echo "make-portable: de vorige run is midden in de wissel van User/ afgebroken; User.old teruggezet als User/"
        mv -T -- "$OLDUSER" "$USERDIR"
    fi
    rm -f -- "$SWAPMARK"
fi
recover_app_dirs
remove_tree "$NEWUSER" "$PATCH_TMP"
rm -f -- "$LAUNCHER_TMP"
# Geen fix_perms op de bestaande User/: die blijft ongemoeid (de games maken er mode-000
# overlay-mapjes in); bij --reset-user wordt hij aan het eind User.old.

if [[ ! -d "$USERDIR" || "$RESET_USER" == 1 ]]; then
    BUILD_USER=1
    # Draait er iets vanuit User/, dan niet eens beginnen: de wissel zou het straks toch weigeren.
    if [[ -d "$USERDIR" ]] && ! is_free "$USERDIR"; then
        die "User/ is in gebruik (zie hierboven: een game of programma draait vanuit de map); stop het en draai opnieuw"
    fi
fi
# User.old: weg aan het begin van een --reset-user (de naam is straks nodig, en zijn ruimte telt mee in de ruimtecontrole)
# of met --drop-old; anders blijft hij staan (ook als User/ ontbreekt en opnieuw opgebouwd wordt). Eerst de goedkope
# controles (in gebruik, rsync, ruimte), pas dan weggooien: een run die toch niet kan, gooit niets weg.
DROP_NOW=0
if [[ -e "$OLDUSER" || -L "$OLDUSER" ]]; then
    if [[ "$RESET_USER" == 1 || "$DROP_OLD" == 1 ]]; then
        DROP_NOW=1
        is_free "$OLDUSER" ||
            die "User.old (de User/ van vóór de vorige --reset-user) is nog in gebruik (zie hierboven); stop wat er vanuit draait en draai opnieuw"
    else
        echo "make-portable: User.old (de User/ van vóór de vorige --reset-user) blijft staan; weggooien met --drop-old"
    fi
fi
if [[ "$BUILD_USER" == 1 ]]; then
    preflight_user "$DROP_NOW"     # rsync en ruimte, vóór er iets veranderd of weggegooid is
fi
if [[ "$DROP_NOW" == 1 ]]; then
    rc=0; drop_old_user || rc=$?
    case "$rc" in
        0) ;;
        1) die "User.old (de User/ van vóór de vorige --reset-user) is nog in gebruik (zie hierboven); stop wat er vanuit draait en draai opnieuw" ;;
        *) die "User.old weggooien mislukt; ruim hem zelf op (chmod -R u+rwX '$OLDUSER' && rm -rf '$OLDUSER') en draai opnieuw" ;;
    esac
fi

refresh_app

if [[ "$BUILD_USER" == 1 ]]; then
    echo "make-portable: gebruikersmap opbouwen"
    BUILDING=1
    ACTUAL_USER="$NEWUSER"
    mkdir -p "$NEWUSER"/{config,state,cache,share/ravengun}
    copy_config
    copy_runtime
    restore_src_perms   # de bron weer precies zoals hij was (mode-000-werkmappen weer 000)
else
    echo "make-portable: User/ blijft staan; alleen bin/, lib/ en de profielen verversen (--reset-user voor een volledige herbouw)"
fi

write_launcher

# Eerst de koppelingen (een wrapper die een koppeling naar een repo-script was, is daarna een echt bestand in de map), dan pas
# de wrappers en de paden in de configuratie herschrijven: nooit door een koppeling heen naar de repo of ~/.local.
relink || die "er zijn koppelingen die niet in de map passen (zie hierboven); de map is niet bruikbaar als losse map"

if [[ "$BUILD_USER" == 1 ]]; then
    fix_scripts
    rewrite_config_paths
fi

if [[ "$BUILD_USER" == 1 ]]; then
    # De wissel. Niet zolang een proces User/ gebruikt: een game die vanuit de map draait zou zijn schrijflaag, prefix en
    # saves onder zich zien verdwijnen. Twee renames, tijdens welke we niet onderbroken willen worden.
    if [[ -d "$USERDIR" ]] && ! is_free "$USERDIR"; then
        die "User/ is in gebruik (zie hierboven); de wissel gaat niet door. Stop wat er vanuit de map draait en draai opnieuw"
    fi
    trap '' INT TERM
    if [[ -d "$USERDIR" ]]; then
        [[ ! -e "$OLDUSER" && ! -L "$OLDUSER" ]] || die "User.old staat er (weer); de wissel gaat niet door"
        # Het merk zegt de volgende run: "User/ ontbreekt en User.old staat er? Dan is dit een onderbroken wissel."
        echo "wissel van User/ bezig" > "$SWAPMARK"; sync -- "$SWAPMARK" 2>/dev/null || true
        mv -T -- "$USERDIR" "$OLDUSER" || { rm -f -- "$SWAPMARK"; die "User/ opzij zetten (User.old) mislukt; User/ blijft staan"; }
    fi
    if ! mv -T -- "$NEWUSER" "$USERDIR"; then
        if [[ -e "$SWAPMARK" && -d "$OLDUSER" && ! -e "$USERDIR" ]] && mv -T -- "$OLDUSER" "$USERDIR"; then
            rm -f -- "$SWAPMARK"
            die "User.new op zijn plek zetten mislukt; de oude User/ is teruggezet"
        fi
        BUILDING=0     # niets weggooien: de volgende run zet User.old terug (het merk blijft staan)
        die "User.new op zijn plek zetten mislukt en de oude User/ terugzetten ook; User.new en User.old blijven staan"
    fi
    [[ -d "$USERDIR" && ! -e "$NEWUSER" ]] || die "de wissel van User/ is niet gelukt"
    rm -f -- "$SWAPMARK"
    BUILDING=0
    ACTUAL_USER="$USERDIR"
    trap 'exit 130' INT
    trap 'exit 143' TERM
    if [[ -d "$OLDUSER" ]]; then
        echo "make-portable: de vorige User/ staat als User.old ernaast (een game die er nog vanuit draait houdt zo zijn schrijflaag,"
        echo "  prefix en saves); hij blijft tot de volgende --reset-user of een run met --drop-old"
    fi
fi

echo "make-portable: klaar: $DEST (start met $DEST/ravengun.sh app)"
