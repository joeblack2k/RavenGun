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
#   RG_PORTABLE_PATCHES, RG_PORTABLE_EXTRA_FREE (bytes vrije ruimte bovenop de runtime).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST=""
RESET_USER=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --reset-user) RESET_USER=1; shift ;;
        -h|--help) sed -n '2,34p' "$0"; exit 0 ;;
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
PATCHES="${RG_PORTABLE_PATCHES:-$ROOT/patches}"
EXTRA_FREE="${RG_PORTABLE_EXTRA_FREE:-1073741824}"

USERDIR="$DEST/User"
NEWUSER="$DEST/User.new"
OLDUSER="$DEST/User.old"
ACTUAL_USER="$USERDIR"   # waar User/ nu echt staat: tijdens de opbouw User.new
BUILDING=0               # 1 zolang User.new onderweg is (dan ruimt de EXIT-val hem op)

# Wat "de thuismap-indeling" is: hier mag na afloop niets meer naar wijzen.
HOME_ROOTS=("$HOME/.local" "$HOME/.config/ravengun" "$RUNTIME" "$INSTALL" "$CONFIG" "$TUI_PROTON" "$ROOT")

die() { echo "make-portable: $*" >&2; exit 1; }

if [[ ! -x "$INSTALL/bin/ravengund" ]]; then
    die "$INSTALL/bin/ravengund ontbreekt; bouw en installeer eerst (scripts/build-app.sh)"
fi

# Onleesbare overlay-werkmapjes (mode 000, van bwrap; ze ontstaan ook in $DEST/User als
# een game vanuit de map draait) weer leesbaar maken, anders mislukken kopiëren en
# opruimen. Een geneste mode-000-map ziet find pas als de buitenste leesbaar is, dus
# herhalen tot er niets meer te herstellen valt.
fix_perms() {
    local d i
    local -a bad
    for d in "$@"; do
        [[ -e "$d" ]] || continue
        for i in 1 2 3 4 5 6 7 8 9 10; do
            bad=()
            mapfile -d '' -t bad < <(find "$d" \( \( -type d ! -perm -u+rwx \) -o \( -type f ! -perm -u+rw \) \) -print0 2>/dev/null || true)
            (( ${#bad[@]} )) || break
            chmod u+rwX -- "${bad[@]}" 2>/dev/null || true
        done
    done
}

# Een boom weggooien, nadat de rechten hersteld zijn (anders faalt rm onder set -e en
# blijft er 19 GB achter).
remove_tree() {
    local d
    for d in "$@"; do
        [[ -e "$d" || -L "$d" ]] || continue
        fix_perms "$d"
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
    exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# 1. De applicatie zelf, als vervanging (niet samenvoegen): bin/ en lib/ worden in één
# keer vernieuwd, share/ ook. De profielen komen uit de repo, niet uit de installatie
# (die legt share/ravengun/patches als koppeling naar ../../patches neer), en alleen
# de levering (tools/dev/ship-patches.py): geen dossiers of ongevolgde bestanden.
refresh_app() {
    local d
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
    if [[ -d "$PATCHES" ]]; then
        remove_tree "$DEST/share/ravengun/patches"
        mkdir -p "$DEST/share/ravengun"
        python3 -I "$ROOT/tools/dev/ship-patches.py" "$PATCHES" "$DEST/share/ravengun/patches"
    fi
}

# 2. Voor het kopiëren van de runtime: rsync aanwezig en genoeg ruimte? Dit gebeurt
# voordat er iets aangeraakt is, zodat een afbreker niets achterlaat.
preflight_user() {
    local need avail
    command -v rsync >/dev/null || die "rsync ontbreekt (nodig om de runtime te kopiëren)"
    fix_perms "$RUNTIME"   # du en rsync mogen niet struikelen over de mode-000 werkmapjes
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
        rsync -a "$path" "$dst/" || rc=$?
        rsync_ok "$rc" || die "$name kopiëren mislukt (rsync-code $rc); de bestaande map blijft ongemoeid"
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

# 5. Alle koppelingen die naar ~/.local wijzen herschrijven naar een relatieve koppeling
# binnen de map. Wat daarna nog absoluut naar ~/.local wijst (bijvoorbeeld de
# gstreamer-plugins van een Steam-Proton) wordt uitgepakt: de inhoud wordt gekopieerd.
#
# Tijdens de opbouw staat User/ nog als User.new; de relatieve koppelingen moeten
# kloppen voor de plek waar hij straks staat, dus rekenen we met het eindpad
# (final_path) en kijken we op schijf op het huidige pad (actual_path).
final_path() {
    case "$1" in
        "$ACTUAL_USER") printf '%s' "$USERDIR" ;;
        "$ACTUAL_USER"/*) printf '%s' "$USERDIR/${1#"$ACTUAL_USER"/}" ;;
        *) printf '%s' "$1" ;;
    esac
}
actual_path() {
    case "$1" in
        "$USERDIR") printf '%s' "$ACTUAL_USER" ;;
        "$USERDIR"/*) printf '%s' "$ACTUAL_USER/${1#"$USERDIR"/}" ;;
        *) printf '%s' "$1" ;;
    esac
}

# Doel binnen de map? Het eindpad komt in MAPPED. Ook als een map in de runtime
# hernoemd is (bijvoorbeeld TeknoParrotUi-2.0.0.130-linux-x64 -> .verborgen) wijzen we
# naar de juiste plek, met het hele restpad (niet alleen de bestandsnaam).
map_target() {
    local t="$1" nt d rest
    MAPPED=""
    case "$t" in
        "$RUNTIME"/*) nt="$USERDIR/share/ravengun/${t#"$RUNTIME"/}" ;;
        "$INSTALL"/*) nt="$DEST/${t#"$INSTALL"/}" ;;
        "$CONFIG"/*) nt="$USERDIR/config/ravengun/${t#"$CONFIG"/}" ;;
        "$TUI_PROTON"/*) nt="$USERDIR/share/ravengun/proton/${t#"$TUI_PROTON"/}" ;;
        *) return 1 ;;
    esac
    if [[ -e "$(actual_path "$nt")" ]]; then MAPPED="$nt"; return 0; fi
    d="${nt%/*}"
    rest="${nt##*/}"
    while [[ "$d" != "/" && "$d" != "." && "$d" != "$DEST" && -n "$d" ]]; do
        if [[ -e "$(actual_path "$d.verborgen/$rest")" ]]; then
            MAPPED="$d.verborgen/$rest"
            return 0
        fi
        rest="${d##*/}/$rest"
        d="${d%/*}"
    done
    return 1
}

# find-patroon voor alle doelen die in de thuismap-indeling liggen. Voorfilter: van de
# ~6400 koppelingen wijst het merendeel naar de NAS en doet hier niet aan mee.
find_home_links() {
    local -a expr=()
    local r
    for r in "$@"; do
        (( ${#expr[@]} )) && expr+=(-o)
        expr+=(-lname "$r/*")
    done
    find "$DEST" -type l \( "${expr[@]}" \) -printf '%p\0%l\0' 2>/dev/null
}

relink() {
    local l t lf rel sz round extracted
    for round in 1 2 3; do
        # a. koppelingen waarvan het doel mee gekopieerd is: relatief naar de kopie.
        while IFS= read -r -d '' l && IFS= read -r -d '' t; do
            map_target "$t" || continue
            lf="$(final_path "$l")"
            # -s: niets oplossen. Zonder -s wordt een doel dat zelf een absolute koppeling
            # is naar zijn eindpunt vertaald en wijst de relatieve koppeling uit de map.
            rel="$(realpath -s --relative-to="${lf%/*}" "$MAPPED")"
            ln -sfn "$rel" "$l"
        done < <(find_home_links "$RUNTIME" "$INSTALL" "$CONFIG" "$TUI_PROTON")

        # b. wat nog naar ~/.local of de repo wijst (en geen spelinstallatie op de NAS is)
        # uitpakken.
        extracted=0
        while IFS= read -r -d '' l && IFS= read -r -d '' t; do
            case "$t" in "$DEST"/*) continue ;; esac
            if [[ -f "$t" ]]; then
                # -L: een keten van absolute koppelingen wordt de echte inhoud, geen
                # absolute koppeling in de map.
                rm -f "$l.tmp"
                if cp -aL "$t" "$l.tmp" 2>/dev/null; then
                    mv -f "$l.tmp" "$l"
                    extracted=1
                else
                    rm -f "$l.tmp"
                fi
            elif [[ -d "$t" ]]; then
                # Een map uitpakken kan; een grote cache (Steam appcache, honderden MB's)
                # wordt een lege map, die maakt de eigenaar zelf weer vol.
                sz="$(du -sb "$t" 2>/dev/null | cut -f1 || true)"
                rm -f "$l"
                if [[ -n "$sz" && "$sz" -le 134217728 ]]; then
                    cp -a "$t" "$l" 2>/dev/null || mkdir -p "$l"
                    extracted=1   # koppelingen in de kopie krijgen een nieuwe ronde
                else
                    mkdir -p "$l"
                    echo "  cache-koppeling vervangen door lege map: $l (was $(( ${sz:-0} / 1048576 )) MB)"
                fi
            fi
        done < <(find_home_links "${HOME_ROOTS[@]}")
        (( extracted )) || break
    done
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

# 7. De paden in de configuratie van de app (library.json, config.json) wijzen naar
# ~/.local; zet ze op de map. ravengun.sh doet hetzelfde bij elke start, zodat de map
# ook werkt als hij verhuist.
rewrite_config_paths() {
    python3 - "$DEST" "$ACTUAL_USER" <<'PYEOF'
import json, os, sys
dest, actual_user = sys.argv[1], sys.argv[2]
for name in ('library.json', 'config.json'):
    p = os.path.join(actual_user, 'config/ravengun', name)
    if not os.path.exists(p):
        continue
    try:
        with open(p, encoding='utf-8') as f:
            data = json.load(f)
    except Exception:
        continue

    def fix(s):
        for m, root in (('/User/share/ravengun/', 'User/share/ravengun'),
                        ('/.local/share/ravengun/', 'User/share/ravengun'),
                        ('/.config/ravengun/', 'User/config/ravengun')):
            i = s.find(m)
            if i < 0:
                continue
            return os.path.join(dest, root, s[i + len(m):])
        return s

    def walk(o):
        if isinstance(o, dict):
            return {k: walk(v) for k, v in o.items()}
        if isinstance(o, list):
            return [walk(v) for v in o]
        return fix(o) if isinstance(o, str) else o

    out = walk(data)
    if out != data:
        with open(p, 'w', encoding='utf-8') as f:
            json.dump(out, f, ensure_ascii=False, indent=2)
            f.write('\n')
        print('  paden: {} bijgewerkt'.format(p))
PYEOF
}

# 8. De starter: zet de omgeving op de map en kies een binary.
write_launcher() {
    cat > "$DEST/ravengun.sh" <<'EOF'
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
python3 - "$HERE" <<'PYEOF' >/dev/null 2>&1 || true
import json, os, sys
here = sys.argv[1]
base = os.path.join(here, 'User/config/ravengun')
for name in ('library.json', 'config.json'):
    p = os.path.join(base, name)
    if not os.path.exists(p):
        continue
    try:
        data = json.load(open(p, encoding='utf-8'))
    except Exception:
        continue
    def fix(s):
        for m, root in (('/User/share/ravengun/', 'User/share/ravengun'),
                        ('/.local/share/ravengun/', 'User/share/ravengun'),
                        ('/.config/ravengun/', 'User/config/ravengun')):
            i = s.find(m)
            if i < 0:
                continue
            return os.path.join(here, root, s[i + len(m):])
        return s
    def walk(o):
        if isinstance(o, dict):
            return {k: walk(v) for k, v in o.items()}
        if isinstance(o, list):
            return [walk(v) for v in o]
        return fix(o) if isinstance(o, str) else o
    out = walk(data)
    if out != data:
        with open(p, 'w', encoding='utf-8') as f:
            json.dump(out, f, ensure_ascii=False, indent=2)
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
    chmod +x "$DEST/ravengun.sh"
}

# 9. Controle op de hele map: geen enkele koppeling mag nog naar de thuismap-indeling
# wijzen (ook niet via een keten van absolute koppelingen) en geen enkele relatieve
# koppeling mag uit de map wijzen. Absolute koppelingen naar buiten die niet in de
# thuismap-indeling liggen (de spelinstallaties op de NAS) blijven zoals ze zijn en
# worden alleen geteld. Exit 1 bij een overtreding.
verify_links() {
    python3 -I - "$DEST" "$ACTUAL_USER" "$USERDIR" "${HOME_ROOTS[@]}" <<'PYEOF'
import os, sys

dest, actual_user, userdir = sys.argv[1:4]
roots = [r.rstrip('/') for r in sys.argv[4:]]


def inside(p, root):
    return p == root or p.startswith(root.rstrip('/') + '/')


def final(p):
    return userdir + p[len(actual_user):] if inside(p, actual_user) else p


bad, total, buiten = [], 0, 0
for dp, dns, fns in os.walk(dest):
    for n in dns + fns:
        p = os.path.join(dp, n)
        if not os.path.islink(p):
            continue
        total += 1
        t = os.readlink(p)
        lf = final(p)
        if os.path.isabs(t):
            probe, resolved = os.path.normpath(t), os.path.realpath(t)
            if any(inside(probe, r) or inside(resolved, r) for r in roots):
                bad.append((lf, t, 'wijst nog naar de thuismap-indeling'))
            elif not inside(probe, dest):
                buiten += 1
        else:
            probe = os.path.normpath(os.path.join(os.path.dirname(lf), t))
            if not inside(probe, dest):
                bad.append((lf, t, 'relatieve koppeling wijst uit de map'))

print('  koppelingen: {}, absoluut naar buiten (NAS e.d.): {}, fout: {}'.format(total, buiten, len(bad)))
for lf, t, why in bad[:30]:
    print('  FOUT: {} -> {} ({})'.format(lf, t, why), file=sys.stderr)
if len(bad) > 30:
    print('  ... en nog {} andere'.format(len(bad) - 30), file=sys.stderr)
sys.exit(1 if bad else 0)
PYEOF
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
remove_tree "$NEWUSER" "$OLDUSER" "$DEST/bin.new" "$DEST/lib.new"
fix_perms "$DEST"   # de games maken in User/ mode-000 overlay-mapjes

BUILD_USER=0
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

relink
fix_scripts
rewrite_config_paths
write_launcher

verify_links || die "er zijn koppelingen die niet in de map passen (zie hierboven); de map is niet bruikbaar als losse map"

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
