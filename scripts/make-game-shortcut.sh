#!/usr/bin/env bash
# Maakt (of verwijdert) een bureaubladsnelkoppeling voor een game die via RavenGun start.
# Gebruik: scripts/make-game-shortcut.sh PROFIEL "Naam op het bureaublad"
#          scripts/make-game-shortcut.sh --remove PROFIEL
# Het profiel is de naam in TeknoParrot (UserProfiles/<PROFIEL>.xml); de snelkoppeling start
# scripts/play-halo.sh --profile PROFIEL (die zelf de juiste route kiest: TeknoParrot of de Unity-route).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/scripts/rg-paths.sh"   # RG_PREFIX is gevuld bij een installatie
TP="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64"
DESK="$HOME/Desktop"; OUT="$ROOT/packaging/desktop"
# Bij een installatie hoort het bestand niet in de installatiemap (die wordt bij een update vervangen): onder de gebruikersdata.
[[ -z $RG_PREFIX ]] || OUT="${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/shortcuts"
if [[ "${1:-}" == --remove ]]; then
  P="${2:?profiel}"; rm -f "$DESK/game-$P.desktop" "$OUT/game-$P.desktop"; echo "Verwijderd: $P"; exit 0
fi
P="${1:?profiel}"; NAME="${2:?naam}"
[[ "$P" =~ ^[A-Za-z0-9_-]+$ ]] || { echo "Ongeldige profielnaam" >&2; exit 2; }
[[ -f "$TP/UserProfiles/$P.xml" ]] || { echo "Onbekend profiel: $P" >&2; exit 1; }
ICON="$TP/Icons/$P.png"; [[ -f "$ICON" ]] || ICON=input-gaming
mkdir -p "$OUT"
cat > "$OUT/game-$P.desktop" <<EOD
[Desktop Entry]
Type=Application
Name=$NAME
Comment=Start $NAME met de Move als lightgun (RavenGun)
Exec=$ROOT/scripts/play-halo.sh --profile $P
Icon=$ICON
Terminal=false
Categories=Game;
EOD
chmod +x "$OUT/game-$P.desktop"
ln -sfn "$OUT/game-$P.desktop" "$DESK/game-$P.desktop"
gio set "$DESK/game-$P.desktop" metadata::trusted true 2>/dev/null || true
echo "Snelkoppeling: $DESK/game-$P.desktop"
