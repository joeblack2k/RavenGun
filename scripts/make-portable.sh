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
# een bestaande User/ blijft bij een fout ongemoeid.
#
# Gebruik: scripts/make-portable.sh [--reset-user] [DOEL]
#   --reset-user  bouw heel User/ opnieuw op (anders blijft User/ staan en verversen
#                 we alleen bin/, lib/ en share/ravengun/patches). De oude User/ blijft
#                 tot het eind staan; er is dus tijdelijk ruimte voor twee kopieën nodig.
#   DOEL          de map om te vullen (standaard dist/RavenGun); wordt aangemaakt.
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
while [[ $# -gt 0 ]]; do
    case "$1" in
        --reset-user) RESET_USER=1; shift ;;
        -h|--help) sed -n '2,42p' "$0"; exit 0 ;;
        *) DEST="$1"; shift ;;
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
HOME_ROOTS=("$HOME/.local" "$HOME/.config/ravengun" "$RUNTIME" "$INSTALL" "$CONFIG" "$TUI_PROTON" "$ROOT")

die() { echo "make-portable: $*" >&2; exit 1; }

if [[ ! -x "$INSTALL/bin/ravengund" ]]; then
    die "$INSTALL/bin/ravengund ontbreekt; bouw en installeer eerst (scripts/build-app.sh)"
fi

# Onleesbare overlay-werkmapjes (mode 000, van bwrap; ze ontstaan ook in $DEST/User als
# een game vanuit de map draait) weer toegankelijk maken, anders mislukken kopiëren en
# opruimen. Een geneste mode-000-map ziet find pas als de buitenste leesbaar is, dus
# herhalen tot er niets meer te herstellen valt.
#   fix_perms read  PAD...  alleen lezen nodig (de bron): bestanden u+r, mappen u+rx. Nooit u+w:
#                           Wine vertaalt een ontbrekend schrijfbit naar "alleen lezen", dus een
#                           chmod u+w op een live prefix verandert de game stilletjes.
#   fix_perms write PAD...  de boom wordt weggegooid of is van ons: u+rwX.
fix_perms() {
    local mode="$1" d i
    local -a bd bf
    shift
    for d in "$@"; do
        [[ -e "$d" ]] || continue
        for i in 1 2 3 4 5 6 7 8 9 10; do
            bd=(); bf=()
            if [[ "$mode" == write ]]; then
                mapfile -d '' -t bd < <(find "$d" -type d ! -perm -u+rwx -print0 2>/dev/null || true)
                mapfile -d '' -t bf < <(find "$d" -type f ! -perm -u+rw -print0 2>/dev/null || true)
            else
                mapfile -d '' -t bd < <(find "$d" -type d ! -perm -u+rx -print0 2>/dev/null || true)
                mapfile -d '' -t bf < <(find "$d" -type f ! -perm -u+r -print0 2>/dev/null || true)
            fi
            (( ${#bd[@]} + ${#bf[@]} )) || break
            if [[ "$mode" == write ]]; then
                (( ${#bd[@]} )) && { chmod u+rwx -- "${bd[@]}" 2>/dev/null || true; }
                (( ${#bf[@]} )) && { chmod u+rw -- "${bf[@]}" 2>/dev/null || true; }
            else
                (( ${#bd[@]} )) && { chmod u+rx -- "${bd[@]}" 2>/dev/null || true; }
                (( ${#bf[@]} )) && { chmod u+r -- "${bf[@]}" 2>/dev/null || true; }
            fi
        done
    done
    return 0
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

# Bij elke afloop die niet geslaagd is (fout, Ctrl-C, set -e) de halve User.new opruimen.
cleanup() {
    local rc=$?
    if [[ "$BUILDING" == 1 ]]; then
        echo "make-portable: niet afgemaakt; $NEWUSER wordt opgeruimd (een bestaande User/ blijft staan)" >&2
        remove_tree "$NEWUSER" || true
    fi
    remove_tree "$PATCH_TMP" || true
    exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# 1. De applicatie zelf, als vervanging (niet samenvoegen): bin/ en lib/ worden in één
# keer vernieuwd, share/ ook. De profielen komen uit de GECOMMITTE repo (git archive HEAD
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
        remove_tree "$DEST/$d"
        mv "$DEST/$d.new" "$DEST/$d"
    done
    if [[ -e "$INSTALL/share" ]]; then
        remove_tree "$DEST/share"
        cp -a "$INSTALL/share" "$DEST/share"
    fi
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
        remove_tree "$DEST/share/ravengun/patches"
        mkdir -p "$DEST/share/ravengun"
        python3 -I "$ROOT/tools/dev/ship-patches.py" "$src" "$DEST/share/ravengun/patches"
    fi
    remove_tree "$PATCH_TMP"
}

# 2. Voor het kopiëren van de runtime: rsync aanwezig en genoeg ruimte? Dit gebeurt
# voordat er iets aangeraakt is, zodat een afbreker niets achterlaat.
preflight_user() {
    local need avail
    command -v rsync >/dev/null || die "rsync ontbreekt (nodig om de runtime te kopiëren)"
    fix_perms read "$RUNTIME"   # du en rsync mogen niet struikelen over de mode-000 werkmapjes (alleen lees-/zoekrechten)
    need="$(du -sb --exclude=bench-captures --exclude=halo-overlay "$RUNTIME" 2>/dev/null | cut -f1 || true)"
    avail="$(df -B1 --output=avail "$DEST" 2>/dev/null | tail -1 || true)"
    if [[ -n "$need" && -n "$avail" && "$avail" -lt $((need + EXTRA_FREE)) ]]; then
        echo "make-portable: te weinig ruimte in $DEST: nodig ~$((need / 1048576)) MB, vrij $((avail / 1048576)) MB" >&2
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
    local dst="$NEWUSER/share/ravengun" path name rc
    mkdir -p "$dst"
    for path in "$RUNTIME"/*; do
        [[ -e "$path" || -L "$path" ]] || continue
        name="$(basename "$path")"
        case "$name" in bench-captures|halo-overlay) continue ;; esac
        rc=0
        # -X: de user.overlay.*-xattrs van de schrijflagen. Zonder rootrechten slaat rsync char-devices
        # over (de whiteouts); portable_tools.py whiteouts maakt ze na en controleert de aantallen.
        rsync -aX --info=nonreg0 "$path" "$dst/" || rc=$?
        rsync_ok "$rc" || die "$name kopiëren mislukt (rsync-code $rc); de bestaande map blijft ongemoeid"
        python3 -I "$TOOLS" whiteouts "$path" "$dst/$name" ||
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
# boven de map van het script).
fix_scripts() {
    python3 - "$ACTUAL_USER" <<'PYEOF'
import os, sys
actual_user = sys.argv[1]
old = '$HOME/.local/share/ravengun'
new = '$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)'
root = os.path.join(actual_user, 'share/ravengun/wrappers')
for dirpath, _dirs, files in os.walk(root):
    for name in files:
        p = os.path.join(dirpath, name)
        try:
            with open(p, encoding='utf-8') as f:
                s = f.read()
        except Exception:
            continue
        if old in s:
            with open(p, 'w', encoding='utf-8') as f:
                f.write(s.replace(old, new))
            print('  wrapper: {} pakt de runtime nu uit de map'.format(p))
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
for name in ('library.json', 'config.json'):
    p = os.path.join(base, name)
    if not os.path.exists(p):
        continue
    try:
        with open(p, encoding='utf-8') as f:
            data = json.load(f)
    except Exception:
        continue
    out = walk(data)
    if out != data:
        with open(p, 'w', encoding='utf-8') as f:
            json.dump(out, f, ensure_ascii=False, indent=2)
            f.write('\n')
        print('  paden: {} bijgewerkt'.format(p))
PYEOF
)"

# 7. De paden in de configuratie van de nieuwe User zetten op de map.
rewrite_config_paths() {
    python3 -I -c "$PATHFIX_PY" "$DEST" "$ACTUAL_USER/config/ravengun"
}

# 8. De starter: zet de omgeving op de map en kies een binary.
write_launcher() {
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
python3 -I - "$HERE" "$HERE/User/config/ravengun" <<'PYEOF' >/dev/null 2>&1 || true
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
    } > "$DEST/ravengun.sh"
    chmod +x "$DEST/ravengun.sh"
}

# --- uitvoeren -----------------------------------------------------------------
echo "make-portable: $INSTALL -> $DEST"
mkdir -p "$DEST"

# Resten van een eerdere onderbroken run: halfgeruilde User/ terugzetten en tijdelijke
# mappen weg (met herstel van de rechten, anders faalt rm).
if [[ ! -d "$USERDIR" && -d "$OLDUSER" ]]; then
    echo "make-portable: User/ ontbrak maar User.old bestaat (onderbroken wissel); terugzetten"
    mv "$OLDUSER" "$USERDIR"
fi
remove_tree "$NEWUSER" "$OLDUSER" "$DEST/bin.new" "$DEST/lib.new" "$PATCH_TMP"
# Geen fix_perms op de bestaande User/: die blijft ongemoeid (de games maken er mode-000
# overlay-mapjes in) en wordt bij --reset-user aan het eind met schrijfrechten weggegooid.

if [[ ! -d "$USERDIR" || "$RESET_USER" == 1 ]]; then
    BUILD_USER=1
    preflight_user      # rsync en ruimte, vóór er iets veranderd is
fi

refresh_app

if [[ "$BUILD_USER" == 1 ]]; then
    echo "make-portable: gebruikersmap opbouwen"
    BUILDING=1
    ACTUAL_USER="$NEWUSER"
    mkdir -p "$NEWUSER"/{config,state,cache,share/ravengun}
    copy_config
    copy_runtime
else
    echo "make-portable: User/ blijft staan; alleen bin/, lib/ en de profielen verversen (--reset-user voor een volledige herbouw)"
fi

if [[ "$BUILD_USER" == 1 ]]; then
    fix_scripts
    rewrite_config_paths
fi
write_launcher

relink || die "er zijn koppelingen die niet in de map passen (zie hierboven); de map is niet bruikbaar als losse map"

if [[ "$BUILD_USER" == 1 ]]; then
    # De wissel: twee renames, tijdens welke we niet onderbroken willen worden. Mislukt
    # het toch tussen de twee, dan zet de volgende run User.old terug.
    trap '' INT TERM
    if [[ -d "$USERDIR" ]]; then mv "$USERDIR" "$OLDUSER"; fi
    mv "$NEWUSER" "$USERDIR"
    BUILDING=0
    ACTUAL_USER="$USERDIR"
    trap 'exit 130' INT
    trap 'exit 143' TERM
    remove_tree "$OLDUSER"
fi

echo "make-portable: klaar: $DEST (start met $DEST/ravengun.sh app)"
