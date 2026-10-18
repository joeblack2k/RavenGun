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
#   <map>/User                     per-gebruiker: config/, state/, cache/ en share/ (wine, prefixes, openparrot, ...)
#   <map>/ravengun.sh              de starter die de omgeving op de map zet en een binary kiest
#
# De map is zelfstandig: de runtime (wine/proton, de prefixen, de OpenParrot-build),
# de staat van de games (views, schrijflagen, SRAM/regiogegevens, HASP-antwoorden) en
# de configuratie worden ECHT gekopieerd, niet gekoppeld. Alle koppelingen die naar
# ~/.local wezen worden daarna herschreven naar een relatieve koppeling binnen de map
# (of uitgepakt als dat niet kan). Na het kopiëren heeft ~/.local niets meer nodig;
# alleen de spelinstallaties (de NAS) blijven buiten de map.
#
# Gebruik: scripts/make-portable.sh [--reset-user] [DOEL]
#   --reset-user  gooi User/ weg en bouw het opnieuw op (anders blijft User/ staan
#                 en verversen we alleen bin/, lib/ en share/ravengun/patches)
#   DOEL          de map om te vullen (standaard dist/RavenGun); wordt aangemaakt.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST=""
RESET_USER=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --reset-user) RESET_USER=1; shift ;;
        -h|--help) sed -n '2,24p' "$0"; exit 0 ;;
        *) DEST="$1"; shift ;;
    esac
done
DEST="${DEST:-$ROOT/dist/RavenGun}"
INSTALL="$HOME/.local/opt/ravengun"
RUNTIME="$HOME/.local/share/ravengun"
CONFIG="$HOME/.config/ravengun"
# De prefixen in de runtime linken naar de Proton-build van de app; die staat buiten de
# runtime maar is identiek aan $RUNTIME/proton (gecontroleerd 2026-10-18, zelfde md5).
TUI_PROTON="$HOME/.local/share/TeknoParrotUI/proton"

if [[ ! -x "$INSTALL/bin/ravengund" ]]; then
    echo "make-portable: $INSTALL/bin/ravengund ontbreekt; bouw en installeer eerst (scripts/build-app.sh)" >&2
    exit 1
fi

echo "make-portable: $INSTALL -> $DEST"
mkdir -p "$DEST"

# 1. De applicatie zelf, als vervanging (niet samenvoegen): bin/ en lib/ worden in één
# keer vernieuwd, share/ ook. De profielen komen uit de repo, niet uit de installatie
# (die legt share/ravengun/patches als koppeling naar ../../patches neer).
refresh_app() {
    local d
    for d in bin lib; do
        [[ -e "$INSTALL/$d" ]] || continue
        rm -rf "${DEST:?}/$d.new"
        cp -a "$INSTALL/$d" "$DEST/$d.new"
        rm -rf "${DEST:?}/$d"
        mv "$DEST/$d.new" "$DEST/$d"
    done
    if [[ -e "$INSTALL/share" ]]; then
        rm -rf "${DEST:?}/share"
        cp -a "$INSTALL/share" "$DEST/share"
    fi
    if [[ -d "$ROOT/patches" ]]; then
        rm -rf "${DEST:?}/share/ravengun/patches"
        mkdir -p "$DEST/share/ravengun/patches"
        cp -a "$ROOT/patches/." "$DEST/share/ravengun/patches/"
    fi
}

# 2. Onleesbare overlay-werkmapjes (mode 000, van bwrap) weer leesbaar maken, anders
# mislukken de kopie en het opruimen van de oude map. Twee rondes: de eerste maakt de
# map zelf leesbaar, de tweede ziet pas wat erin zit.
fix_perms() {
    local i
    for i in 1 2; do
        find "$RUNTIME" \( -type d -o -type f \) ! -perm -u+rw -exec chmod u+rwX {} + 2>/dev/null || true
    done
}

# 3. De runtime en de staat van de games kopiëren. Dat gebeurt in een tijdelijke map
# naast de bestemming; pas als alles gelukt is wordt de oude map weggegooid en de
# nieuwe op zijn plek gezet (een rename, geen 18 GB opnieuw door dezelfde map).
copy_runtime() {
    local tmp="$DEST/User/share/ravengun.new" name need avail
    fix_perms   # eerst: du/rsync mogen niet struikelen over de mode-000 werkmapjes
    need="$(du -sb --exclude=bench-captures --exclude=halo-overlay "$RUNTIME" 2>/dev/null | cut -f1 || true)"
    avail="$(df -B1 --output=avail "$DEST" 2>/dev/null | tail -1 || true)"
    if [[ -n "$need" && -n "$avail" && "$avail" -lt $((need + 1073741824)) ]]; then
        echo "make-portable: te weinig ruimte in $DEST: nodig ~$((need / 1048576)) MB, vrij $((avail / 1048576)) MB" >&2
        exit 1
    fi
    rm -rf "$tmp"
    mkdir -p "$tmp"
    for path in "$RUNTIME"/*; do
        [[ -e "$path" ]] || continue
        name="$(basename "$path")"
        case "$name" in bench-captures|halo-overlay) continue ;; esac
        if rsync -a --delete "$path" "$tmp/" 2>/dev/null; then
            echo "  gekopieerd: $name"
        else
            echo "make-portable: $name niet volledig gekopieerd (overgeslagen)" >&2
        fi
    done
    rm -rf "${DEST:?}/User/share/ravengun"
    mv "$tmp" "$DEST/User/share/ravengun"
}

# 4. De configuratie van deze gebruiker, zonder de geheimen en de back-ups: web-token is
# een sessiesleutel en de backups/*.bak zijn oude kopieën.
copy_config() {
    rm -rf "${DEST:?}/User/config/ravengun"
    mkdir -p "$DEST/User/config/ravengun"
    [[ -d "$CONFIG" ]] || return 0
    rsync -a --exclude web-token --exclude backups --exclude '*.bak' "$CONFIG/" "$DEST/User/config/ravengun/" 2>/dev/null || true
    echo "  config: $CONFIG -> User/config/ravengun (zonder web-token en back-ups)"
}

# 5. Alle koppelingen die naar ~/.local wijzen herschrijven naar een relatieve koppeling
# binnen de map. Wat daarna nog absoluut naar ~/.local wijst (bijvoorbeeld de
# gstreamer-plugins van een Steam-Proton) wordt uitgepakt: de inhoud wordt gekopieerd.
relink() {
    local l t nt rel
    # Doel binnen de map? Ook als een map in de runtime hernoemd is (bijvoorbeeld
    # TeknoParrotUi-2.0.0.130-linux-x64 -> .verborgen) wijzen we naar de juiste plek.
    map_target() {
        local t="$1" nt d
        case "$t" in
            "$RUNTIME"/*) nt="$DEST/User/share/ravengun/${t#"$RUNTIME"/}" ;;
            "$INSTALL"/*) nt="$DEST/${t#"$INSTALL"/}" ;;
            "$CONFIG"/*) nt="$DEST/User/config/ravengun/${t#"$CONFIG"/}" ;;
            "$TUI_PROTON"/*) nt="$DEST/User/share/ravengun/proton/${t#"$TUI_PROTON"/}" ;;
            *) return 1 ;;
        esac
        if [[ -e "$nt" ]]; then printf '%s' "$nt"; return 0; fi
        d="$(dirname "$nt")"
        while [[ "$d" != "/" && "$d" != "." ]]; do
            if [[ -e "${d}.verborgen/$(basename "$nt")" ]]; then
                printf '%s' "${d}.verborgen/$(basename "$nt")"
                return 0
            fi
            d="$(dirname "$d")"
        done
        return 1
    }
    while IFS= read -r -d '' l; do
        t="$(readlink "$l")"
        nt="$(map_target "$t")" || continue
        rel="$(realpath --relative-to="$(dirname "$l")" "$nt")"
        ln -sfn "$rel" "$l"
    done < <(find "$DEST" -type l -print0 2>/dev/null)

    # Wat nog naar ~/.local wijst (en geen spelinstallatie op de NAS is) uitpakken.
    while IFS= read -r -d '' l; do
        t="$(readlink "$l")"
        case "$t" in
            "$HOME"/.local/*|"$ROOT"/*) ;;
            *) continue ;;
        esac
        if [[ -f "$t" ]]; then
            rm -f "$l.tmp"
            if cp -a "$t" "$l.tmp" 2>/dev/null; then
                mv -f "$l.tmp" "$l"
            else
                rm -f "$l.tmp"
            fi
        elif [[ -d "$t" ]]; then
            # Een map uitpakken kan; een grote cache (Steam appcache, honderden MB's)
            # wordt een lege map, die maakt de eigenaar zelf weer vol.
            sz="$(du -sb "$t" 2>/dev/null | cut -f1)"
            rm -f "$l"
            if [[ -n "$sz" && "$sz" -le 134217728 ]]; then
                cp -a "$t" "$l" 2>/dev/null || mkdir -p "$l"
            else
                mkdir -p "$l"
                echo "  cache-koppeling vervangen door lege map: $l (was $(( ${sz:-0} / 1048576 )) MB)"
            fi
        fi
    done < <(find "$DEST" -type l -print0 2>/dev/null)
}

# 6. De wrappers van de Wine-route pakken hun runtime standaard uit
# ~/.local/share/ravengun; in de map horen ze dat uit de map zelf te doen (de map
# boven de map van het script).
fix_scripts() {
    python3 - "$DEST" <<'PYEOF'
import os, sys
dest = sys.argv[1]
old = '$HOME/.local/share/ravengun'
new = '$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)'
root = os.path.join(dest, 'User/share/ravengun/wrappers')
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
    python3 - "$DEST" <<'PYEOF'
import json, os, sys
dest = sys.argv[1]
for name in ('library.json', 'config.json'):
    p = os.path.join(dest, 'User/config/ravengun', name)
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

# --- uitvoeren -----------------------------------------------------------------
refresh_app

if [[ ! -d "$DEST/User" || "$RESET_USER" == 1 ]]; then
    echo "make-portable: gebruikersmap opbouwen"
    mkdir -p "$DEST/User"/{config,state,cache,share/ravengun}
    copy_config
    copy_runtime
else
    echo "make-portable: User/ blijft staan; alleen bin/, lib/ en de profielen verversen (--reset-user voor een volledige herbouw)"
fi

relink
fix_scripts
rewrite_config_paths
write_launcher

links="$(find "$DEST" -type l -lname "$HOME/.local/*" 2>/dev/null | wc -l)"
echo "make-portable: klaar: $DEST (start met $DEST/ravengun.sh app); koppelingen naar ~/.local: $links"
