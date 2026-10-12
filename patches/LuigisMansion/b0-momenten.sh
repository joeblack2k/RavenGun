#!/usr/bin/env bash
# b0-momenten.sh -- de tijdlijn van ONZE kern naast die van de GESLOTEN kern.
#
# Per kern een run via de werkende route, en EEN root-proces dat het spelproces op vaste
# momenten bemonstert (2, 3, 3.5, 4, 5, 6, 8, 10, 12 s na het eerste verschijnen):
#   - staat de entry-point-trampoline er nog, en wordt hij ooit teruggezet?
#   - is .text nog nul (niets ontpakt) of gevuld?
#   - staan de 3494 bytes op 0x14120d000 in .capcom1 zoals in het bestand?
#
# Dat is de vraag met twee adressen, en met de reeks momenten krijg je een tijdlijn in plaats
# van een enkel punt -- nodig omdat het einde tijdafhankelijk is (5-7 s met WINEDEBUG=-all,
# 25-30 s met de +file-trace).
#
# GEBRUIK: bash b0-momenten.sh
set -u
HIER="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="${RG_REPO:-$HOME/projects/ravengun}"
TP="${RAVENGUN_TP_DIR:-$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64}"
KERN="$TP/TeknoParrot/TeknoParrot64.dll"
OPC="$HOME/.local/share/ravengun/openparrot/current/OpenParrotx64/OpenParrot64.dll"
PFX="${RG_WINEPREFIX:-$HOME/.local/share/ravengun/prefixes/shared/wine}"
WINEBIN="${RG_WINE_BIN:-$HOME/.local/share/ravengun/wine/proton-11.0-2/bin}"
MAX="${RG_B0_MAX:-40}"
SUDOPW="${RG_SUDO_PW:-deck}"
UIT="${RG_B0_UIT:-/tmp/b0-momenten-$(date +%Y%m%d-%H%M%S)}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

zeg()  { printf '\033[1m==\033[0m %s\n' "$*"; }
fout() { printf '\033[31mFOUT\033[0m %s\n' "$*" >&2; }

EIGEN=""; _p=$$
while [[ "${_p:-0}" -gt 1 ]]; do EIGEN+="$_p "; _p=$(awk '{print $4}' "/proc/$_p/stat" 2>/dev/null); [[ -z "${_p:-}" ]] && break; done
AAN=""
for pat in 'Game.exe' 'VACUUM.exe' 'OpenParrotLoade' 'BudgieLoader.ex' 'BudgieLoader_x6' \
           'TeknoParrotUi' 'TeknoMacaw.exe' 'TeknoMacaw64.ex' 'gamescope' 'gamescope-wl' \
           'gamescopereaper' 'wineserver' 'rg-jvsbridge.ex' 'pipehelper.exe'; do
  for HIT in $(pgrep -x "$pat" 2>/dev/null || true); do
    [[ " $EIGEN " == *" $HIT "* ]] && continue
    AAN+="  comm $pat -> $HIT"$'\n'
  done
done
[[ -n "$AAN" ]] && { fout "de bench is NIET vrij."; printf '%s' "$AAN"; exit 3; }
zeg "bench vrij"
mkdir -p "$UIT"
ORIG_SHA=$(sha256sum "$KERN" | awk '{print $1}')
BEWAARD="$KERN.b0-bewaard"
herstel_kern() {
  if [[ -f "$BEWAARD" ]]; then rm -f "$KERN"; mv -f "$BEWAARD" "$KERN"; fi
  local nu; nu=$(sha256sum "$KERN" | awk '{print $1}')
  [[ "$nu" == "$ORIG_SHA" ]] || fout "kern-DLL wijkt af: $nu"
}
GSPID=""
opruimen() { herstel_kern; if [[ -n "$GSPID" ]]; then kill -9 -"$GSPID" 2>/dev/null; sleep 0.5; pkill -9 -f 'gamescope --backend headless' 2>/dev/null; fi; }
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

doe_run() {
  local label="$1" variant="$2"
  local bedoeld; bedoeld=$(sha256sum "$variant" | awk '{print $1}')
  if [[ "$variant" != "$KERN" ]]; then
    [[ -f "$BEWAARD" ]] || cp -f "$KERN" "$BEWAARD"
    cp -f "$variant" "$KERN"
  fi
  zeg "run $label -- kern ${bedoeld:0:12}; bemonsteren op 2/3/3,5/4/5/6/8/10/12 s"
  set -m
  ( cd "$REPO" && env DISPLAY="${DISPLAY:-}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}" \
      XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" WINEDEBUG="-all" \
      timeout "$MAX" python3 scripts/launch-direct.py LuigisMansion --run ) >"$UIT/$label.log" 2>&1 &
  local pgid=$!
  set +m
  echo "$SUDOPW" | sudo -S env RG_B0_MOMENTEN="${RG_B0_MOMENTEN:-}" python3 "$HIER/b0-momenten.py" "$UIT" "$label" 16 2>&1 | sed 's/^/  /'
  kill -9 -"$pgid" 2>/dev/null; wait "$pgid" 2>/dev/null
  [[ "$variant" != "$KERN" ]] && herstel_kern
  grep -a -oE 'kern \(runtime\): .* sha256 [0-9a-f]+' "$UIT/$label.log" | head -1 | sed 's/.*sha256 //' > "$UIT/$label.werkelijk-sha"
  veeg
}

doe_run "onze" "$OPC"
doe_run "gesloten" "$KERN"

zeg "tijdlijnen"
{
  echo "kern werkelijk gebruikt:"
  for r in onze gesloten; do printf '  %-10s %s\n' "$r" "$(cat "$UIT/$r.werkelijk-sha" 2>/dev/null)"; done
  echo
  cat "$UIT/momenten-onze.txt" 2>/dev/null
  cat "$UIT/momenten-gesloten.txt" 2>/dev/null
} | tee "$UIT/tijdlijnen.txt"

zeg "alles staat in $UIT"

