#!/usr/bin/env bash
# lm-meet.sh -- EEN run per kern, met de bewezen opzet (headless gamescope + launch-direct),
# en een root-sampler die de trampolinepagina en het dongle-object leest.
#
# GEBRUIK:
#   bash lm-meet.sh                  # met de kern die NU in de runtime staat (geen wissel)
#   RG_LM_VARIANT=onze bash lm-meet.sh   # onze kern (uit openparrot/current) erin, daarna terug
#
# De kern wordt achteraf altijd hersteld en de werkelijke sha wordt uit het launch-direct-log
# gehaald. De bench wordt vooraf gecontroleerd; is hij niet vrij, dan stopt het script.
set -u
HIER="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="${RG_REPO:-$HOME/projects/ravengun}"
TP="${RAVENGUN_TP_DIR:-$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64}"
KERN="$TP/TeknoParrot/TeknoParrot64.dll"
OPC="$HOME/.local/share/ravengun/openparrot/current/OpenParrotx64/OpenParrot64.dll"
PFX="${RG_WINEPREFIX:-$HOME/.local/share/ravengun/prefixes/shared/wine}"
WINEBIN="${RG_WINE_BIN:-$HOME/.local/share/ravengun/wine/proton-11.0-2/bin}"
MAX="${RG_LM_MAX:-40}"
SUDOPW="${RG_SUDO_PW:-deck}"
VARIANT="${RG_LM_VARIANT:-huidig}"
UIT="${RG_LM_UIT:-/tmp/lm-meet-$(date +%Y%m%d-%H%M%S)}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

zeg()  { printf '\033[1m==\033[0m %s\n' "$*"; }
fout() { printf '\033[31mFOUT\033[0m %s\n' "$*" >&2; }

EIGEN=""; _p=$$
while [[ "${_p:-0}" -gt 1 ]]; do EIGEN+="$_p "; _p=$(awk '{print $4}' "/proc/$_p/stat" 2>/dev/null); [[ -z "${_p:-}" ]] && break; done
AAN=""
for pat in 'Game.exe' 'VACUUM.exe' 'OpenParrotLoade' 'BudgieLoader.ex' 'BudgieLoader_x6' \
           'TeknoParrotUi' 'TeknoMacaw.exe' 'TeknoMacaw64.ex' 'gamescope' 'gamescope-wl' \
           'gamescopereaper' 'wineserver' 'rg-jvsbridge.ex' 'pipehelper.exe' 'b0-vijfmin' 'lm-meet'; do
  for HIT in $(pgrep -x "$pat" 2>/dev/null || true); do
    [[ " $EIGEN " == *" $HIT "* ]] && continue
    AAN+="  comm $pat -> $HIT"$'\n'
  done
done
[[ -n "$AAN" ]] && { fout "de bench is NIET vrij."; printf '%s' "$AAN"; exit 3; }
zeg "bench vrij"

mkdir -p "$UIT"
ORIG_SHA=$(sha256sum "$KERN" | awk '{print $1}')
zeg "kern in de runtime bij aanvang: $ORIG_SHA"
BEWAARD="$KERN.lm-bewaard"
herstel_kern() {
  if [[ -f "$BEWAARD" ]]; then rm -f "$KERN"; mv -f "$BEWAARD" "$KERN"; fi
  local nu; nu=$(sha256sum "$KERN" | awk '{print $1}')
  [[ "$nu" == "$ORIG_SHA" ]] || fout "kern-DLL wijkt af na herstel: $nu"
}

if [[ "$VARIANT" == "onze" ]]; then
  [[ -f "$OPC" ]] || { fout "onze kern niet gevonden: $OPC"; exit 1; }
  BEDOELD=$(sha256sum "$OPC" | awk '{print $1}')
  cp -f "$KERN" "$BEWAARD"
  cp -f "$OPC" "$KERN"
  zeg "onze kern ingezet: $BEDOELD"
fi

GSPID=""
opruimen() {
  herstel_kern
  if [[ -n "$GSPID" ]]; then kill -9 -"$GSPID" 2>/dev/null; sleep 0.5; pkill -9 -f 'gamescope --backend headless' 2>/dev/null; fi
  pkill -9 -f 'lm-meet.py' 2>/dev/null
}
trap opruimen EXIT

zeg "headless gamescope starten"
rm -f "$UIT/gamescope.log"
setsid gamescope --backend headless -W 1920 -H 1080 -- sleep 3600 >"$UIT/gamescope.log" 2>&1 </dev/null &
GSPID=$!
DISP=""
for _ in $(seq 1 40); do DISP=$(grep -o 'Starting Xwayland on :[0-9]*' "$UIT/gamescope.log" 2>/dev/null | tail -1 | grep -o '[0-9]*$'); [[ -n "$DISP" ]] && break; sleep 1; done
[[ -n "$DISP" ]] || { fout "gamescope kwam niet op"; exit 1; }
export DISPLAY=":$DISP" WAYLAND_DISPLAY=gamescope-0
zeg "DISPLAY=:$DISP"

veeg() {
  local p c
  for p in $(pgrep -x VACUUM.exe 2>/dev/null); do c=$(tr '\0' ' ' < "/proc/$p/cmdline" 2>/dev/null); [[ "$c" == *"LuigisMansion"* ]] && kill -9 "$p" 2>/dev/null; done
  pkill -9 -f "OpenParrotLoader64.exe.*LuigisMansion" 2>/dev/null
  pkill -9 -f 'rg-jvsbridge' 2>/dev/null; pkill -9 -f 'pipehelper.exe' 2>/dev/null
  sleep 1; WINEPREFIX="$PFX" "$WINEBIN/wineserver" -k 2>/dev/null; sleep 1
}

zeg "run starten en bemonsteren (max ${MAX}s)"
set -m
( cd "$REPO" && env DISPLAY="${DISPLAY:-}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}" \
    XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" WINEDEBUG="${RG_LM_WINEDEBUG:--all}" \
    timeout "$MAX" python3 scripts/launch-direct.py LuigisMansion --run ) >"$UIT/run.log" 2>&1 &
PGID=$!
set +m
echo "$SUDOPW" | sudo -S env RG_LM_MOMENTEN="${RG_LM_MOMENTEN:-}" \
  python3 "$HIER/lm-meet.py" "$UIT" "$VARIANT" "$MAX" 2>&1 | sed 's/^/  /'
kill -9 -"$PGID" 2>/dev/null; wait "$PGID" 2>/dev/null
grep -a -oE 'kern \(runtime\): .* sha256 [0-9a-f]+' "$UIT/run.log" | head -1 | sed 's/.*sha256 //' > "$UIT/werkelijk-sha"
zeg "kern die launch-direct werkelijk gebruikte: $(cat "$UIT/werkelijk-sha" 2>/dev/null)"
zeg "exitcode-lijn: $(grep -a -oE 'lader gestopt \(exitcode [^)]*\)' "$UIT/run.log" | tail -1)"
veeg
zeg "alles staat in $UIT"
