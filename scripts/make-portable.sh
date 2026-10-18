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
# De grote runtime (wine/proton, de prefixen, de OpenParrot-build) hoort in
# <map>/User/share/ravengun. Dit script neemt hem over met symbolische koppelingen
# als hij al in ~/.local/share/ravengun staat; anders moet de gebruiker hem daar
# neerzetten (docs/portable.md, "Wat de gebruiker zelf toevoegt").
#
# Gebruik: scripts/make-portable.sh [DOEL]
#   DOEL   de map om te vullen (standaard dist/RavenGun); wordt aangemaakt.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${1:-$ROOT/dist/RavenGun}"
INSTALL="$HOME/.local/opt/ravengun"
RUNTIME="$HOME/.local/share/ravengun"

if [[ ! -x "$INSTALL/bin/ravengund" ]]; then
    echo "make-portable: $INSTALL/bin/ravengund ontbreekt; bouw en installeer eerst (scripts/build-app.sh)" >&2
    exit 1
fi

echo "make-portable: $INSTALL -> $DEST"
mkdir -p "$DEST"

# 1. De applicatie zelf. bin/ en lib/ kunnen in gebruik zijn (een lopende daemon);
# --remove-destination maakt ze eerst vrij. share/ vernieuwen we in één keer, want
# daarin zit de koppeling share/ravengun/patches -> ../../patches uit de installatie.
for d in bin lib; do
    [[ -e "$INSTALL/$d" ]] || continue
    mkdir -p "$DEST/$d"
    cp -a --remove-destination "$INSTALL/$d/." "$DEST/$d/"
done
if [[ -e "$INSTALL/share" ]]; then
    rm -rf "$DEST/share"
    cp -a "$INSTALL/share" "$DEST/"
fi

# 2. De profielen en drop-ins uit de repo (in de installatie kunnen ze ouder zijn).
if [[ -d "$ROOT/patches" ]]; then
    rm -f "$DEST/share/ravengun/patches"
    mkdir -p "$DEST/share/ravengun/patches"
    cp -a "$ROOT/patches/." "$DEST/share/ravengun/patches/"
fi

# 3. De gebruikersmap met de XDG-mappen.
mkdir -p "$DEST/User"/{config,state,cache,share/ravengun}

# 4. De grote runtime overnemen (symbolisch; geen tweede kopie van gigabytes).
# runtime/, native/, writelayer/ en patches/ worden ECHTE mappen: daarin maakt de
# engine views en schrijflagen, en een koppeling naar buiten weigert hij (terecht:
# een drop-in mag nooit buiten de map terechtkomen).
# De staat van de games (per-game schrijflagen, SRAM/regiogegevens, HASP-antwoorden,
# views) is klein genoeg om mee te nemen; die kopiëren we hard (geen tweede bytes).
# De grote runtime (wine, proton, prefixen) krijgt een koppeling.
copy_state() {
    local name="$1"
    local src="$RUNTIME/$name"
    local dst="$DEST/User/share/ravengun/$name"
    [[ -e "$src" ]] || return 0
    [[ -L "$dst" ]] && rm -f "$dst"
    if [[ -e "$dst" ]]; then
        cp -al "$src/." "$dst/" 2>/dev/null || cp -a "$src/." "$dst/"
    else
        cp -al "$src" "$dst" 2>/dev/null || cp -a "$src" "$dst"
    fi
    echo "  staat: $name (harde koppelingen)"
}
link_runtime() {
    local name="$1"
    local src="$RUNTIME/$name"
    local dst="$DEST/User/share/ravengun/$name"
    [[ -e "$src" ]] || return 0
    if [[ -e "$dst" && ! -L "$dst" ]]; then
        echo "make-portable: $dst bestaat al en is geen koppeling; laat staan" >&2
        return 0
    fi
    ln -sfn "$src" "$dst"
    echo "  runtime: $name -> $src"
}
for name in native writelayer patches runtime hasp memory; do
    copy_state "$name"
done
for name in wine proton prefixes openparrot runtime dxvk redist psmoveapi gst32 bepinex demulshooter cxbx lindbergh-loader loaders mods wrappers jvsbridge hasp memory rio tap tools shortcuts components bin native writelayer; do
    link_runtime "$name"
done
# Wat er verder nog in de gebruikersmap staat (nieuwe onderdelen) ook overnemen.
for path in "$RUNTIME"/*; do
    [[ -e "$path" ]] || continue
    name="$(basename "$path")"
    case "$name" in native|writelayer|patches|runtime|hasp|memory) continue ;; esac
    link_runtime "$name"
done
mkdir -p "$DEST/User/share/ravengun/"{runtime,native,writelayer,patches}

# 5. De starter: zet de omgeving op de map en kies een binary.
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

echo "make-portable: klaar: $DEST (start met $DEST/ravengun.sh app)"
