#!/bin/sh
# scripts/io-route.py (de invoerroute voor play-halo.sh): packaging/io-routes.json plus de keuze van de gebruiker in
# $XDG_CONFIG_HOME/ravengun/io-routes.json, met dezelfde regels als de engine (apply_io_routes_override). Raakt alleen een
# tijdelijke map; skip (77) zonder python3.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
command -v python3 >/dev/null 2>&1 || { echo "geen python3"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
export XDG_CONFIG_HOME="$T/cfg" HOME="$T/home"
mkdir -p "$XDG_CONFIG_HOME/ravengun" "$HOME"
U="$XDG_CONFIG_HOME/ravengun/io-routes.json"
fails=0
q() { python3 -I "$ROOT/scripts/io-route.py" --root "$ROOT" "$@" 2>/dev/null; }
expect() {  # $1 = verwacht, daarna profiel en veld
  want=$1; shift
  got=$(q "$@")
  [ "$got" = "$want" ] || { echo "FAIL: io-route.py $* -> '$got' (verwacht '$want')"; fails=$((fails + 1)); }
}
# Zonder gebruikersbestand: de basis.
expect tp-shm TC5 route
expect tp-shm Halo route
expect evdev OnbekendeGame route
expect direct Halo launch
expect teknoparrot-ui OnbekendeGame launch
expect teknoparrot-ui LGS launch
expect hid-board TransformersShadowsRising route
expect SaeGunBoard TransformersShadowsRising board
expect COM4 LGS com_port
expect base TC5 source
VP=$(q TransformersShadowsRising hid_vidpid)
echo "$VP" | grep -Eq '^[0-9A-F]{4}:[0-9A-F]{4}$' || { echo "FAIL: hid_vidpid '$VP'"; fails=$((fails + 1)); }
# Gebruikerskeuze gaat voor; de rest (layout, board) blijft uit de basis.
printf '{"schema":1,"games":{"TC5":{"route":"evdev"},"Halo":{"route":"evdev","launch":"teknoparrot-ui"}}}\n' > "$U"
expect evdev TC5 route
expect user TC5 source
expect direct TC5 launch
expect evdev Halo route
expect teknoparrot-ui Halo launch
expect WartranTroopers Halo layout
expect hid-board TransformersShadowsRising route
# Ongeldig (tp-shm zonder layout, onbekende route, kapotte JSON): het hele bestand wordt genegeerd, zoals in de engine.
printf '{"games":{"TC5":{"route":"evdev"},"Nieuw":{"route":"tp-shm"}}}\n' > "$U"
expect tp-shm TC5 route
printf '{"games":{"TC5":{"route":"bluetooth"}}}\n' > "$U"
expect tp-shm TC5 route
printf '{"games":{"TC5":{"route":"evdev","launch":"altijd"}}}\n' > "$U"
expect tp-shm TC5 route
printf 'kapot' > "$U"
expect tp-shm TC5 route
python3 -I "$ROOT/scripts/io-route.py" --root "$ROOT" TC5 route 2>&1 >/dev/null | grep -q genegeerd || { echo "FAIL: geen melding bij een ongeldig gebruikersbestand"; fails=$((fails + 1)); }
# Zonder XDG_CONFIG_HOME: dan ~/.config.
rm -f "$U"
unset XDG_CONFIG_HOME
mkdir -p "$HOME/.config/ravengun"
printf '{"games":{"TC5":{"route":"evdev"}}}\n' > "$HOME/.config/ravengun/io-routes.json"
expect evdev TC5 route
# Gebruiksfout.
python3 -I "$ROOT/scripts/io-route.py" TC5 >/dev/null 2>&1
[ $? -eq 2 ] || { echo "FAIL: gebruiksfout niet gemeld"; fails=$((fails + 1)); }
bash -n "$ROOT/scripts/play-halo.sh" || { echo "FAIL: syntaxfout in play-halo.sh"; fails=$((fails + 1)); }
python3 -I -c "import ast, sys; ast.parse(open(sys.argv[1]).read())" "$ROOT/scripts/launch-direct.py" 2>/dev/null || { echo "FAIL: syntaxfout in launch-direct.py"; fails=$((fails + 1)); }
if [ $fails -eq 0 ]; then echo "io-route: alle controles geslaagd"; exit 0; fi
exit 1
