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
# de configuratie worden ECHT gekopieerd, niet gekoppeld. Na het kopiëren heeft
# ~/.local niets meer nodig; alleen de spelinstallaties (de NAS) blijven buiten de map.
#
# Gebruik: scripts/make-portable.sh [DOEL]
#   DOEL   de map om te vullen (standaard dist/RavenGun); wordt aangemaakt.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${1:-$ROOT/dist/RavenGun}"
INSTALL="$HOME/.local/opt/ravengun"
RUNTIME="$HOME/.local/share/ravengun"
CONFIG="$HOME/.config/ravengun"

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
# De installatie legt share/ravengun/patches als koppeling naar ../../patches neer;
# in de portabele map moet het een echte map zijn.
if [[ -d "$ROOT/patches" ]]; then
    rm -f "$DEST/share/ravengun/patches"
    mkdir -p "$DEST/share/ravengun/patches"
    cp -a "$ROOT/patches/." "$DEST/share/ravengun/patches/"
fi

# 3. De gebruikersmap met de XDG-mappen en de configuratie van deze gebruiker.
mkdir -p "$DEST/User"/{config,state,cache,share/ravengun}
if [[ -d "$CONFIG" ]]; then
    rm -rf "$DEST/User/config/ravengun"
    cp -a "$CONFIG" "$DEST/User/config/ravengun"
    echo "  config: $CONFIG -> User/config/ravengun"
fi

# 4. De runtime en de staat van de games: echte kopieën, geen koppelingen.
# bench-captures (de bewijsschermafbeeldingen, enkele GB's) doen niet mee.
mkdir -p "$DEST/User/share/ravengun"
for path in "$RUNTIME"/*; do
    [[ -e "$path" ]] || continue
    name="$(basename "$path")"
    case "$name" in bench-captures|halo-overlay) continue ;; esac
    rm -rf "$DEST/User/share/ravengun/$name"
    if cp -a "$path" "$DEST/User/share/ravengun/$name"; then
        echo "  gekopieerd: $name"
    else
        echo "make-portable: $name niet volledig gekopieerd (overgeslagen)" >&2
    fi
done

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
