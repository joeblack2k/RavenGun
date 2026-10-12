#!/usr/bin/env bash
# b0-geheugen.sh -- instrument A: het spelbeeld op een VAST vroeg moment onder twee kernen
# dumpen en vergelijken.
#
# Twee runs, alles via de werkende route (launch-direct.py --run), headless gamescope:
#   gesloten  = de GESLOTEN kern (leeft)
#   onze      = onze kern (sterft rond 25-30 s)
# Op 8 s na het eerste verschijnen van het spelproces wordt met sudo gedumpt:
#   - het hele nominale beeldbereik (ImageBase .. ImageBase+SizeOfImage)
#   - de entry-point-regio
#   - de anonieme uitvoerbare gebieden uit /proc/<pid>/maps
# Voor elke dump draait eerst beeldcheck.py (levenscheck), zodat er nooit een dood of
# nooit-ontpakt proces gedumpt wordt.
#
# Daarna: geheugen tegen BESTAND (voor de twee gebieden die in het bestand staan) en
# gesloten tegen onze, byte voor byte, met de adressen van de verschillen.
#
# GEBRUIK:  bash b0-geheugen.sh            # beide kernen
#           RG_B0_MOMENT=8 bash b0-geheugen.sh
set -u
HIER="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="${RG_REPO:-$HOME/projects/ravengun}"
TP="${RAVENGUN_TP_DIR:-$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64}"
KERN="$TP/TeknoParrot/TeknoParrot64.dll"
OPC="$HOME/.local/share/ravengun/openparrot/current/OpenParrotx64/OpenParrot64.dll"
PFX="${RG_WINEPREFIX:-$HOME/.local/share/ravengun/prefixes/shared/wine}"
WINEBIN="${RG_WINE_BIN:-$HOME/.local/share/ravengun/wine/proton-11.0-2/bin}"
MOMENT="${RG_B0_MOMENT:-8}"        # seconden na het eerste verschijnen van het spelproces
MAX="${RG_B0_MAX:-45}"
SUDOPW="${RG_SUDO_PW:-deck}"
UIT="${RG_B0_UIT:-/tmp/b0-geheugen-$(date +%Y%m%d-%H%M%S)}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

zeg()  { printf '\033[1m==\033[0m %s\n' "$*"; }
let_op(){ printf '\033[33m!!\033[0m %s\n' "$*"; }
fout() { printf '\033[31mFOUT\033[0m %s\n' "$*" >&2; }

EIGEN=""; _p=$$
while [[ "${_p:-0}" -gt 1 ]]; do EIGEN+="$_p "; _p=$(awk '{print $4}' "/proc/$_p/stat" 2>/dev/null); [[ -z "${_p:-}" ]] && break; done
AAN=""
# Exacte procesnamen (comm, door Linux op 15 tekens afgekapt). GEEN -f: dat matcht de
# hele commandoregel en dus ook elk proces dat de naam alleen noemt.
for pat in 'Game.exe' 'VACUUM.exe' 'OpenParrotLoade' 'BudgieLoader.ex' 'BudgieLoader_x6' \
           'TeknoParrotUi' 'TeknoMacaw.exe' 'TeknoMacaw64.ex' 'gamescope' 'gamescope-wl' \
           'gamescopereaper' 'wineserver' 'rg-jvsbridge.ex' 'pipehelper.exe'; do
  for HIT in $(pgrep -x "$pat" 2>/dev/null || true); do
    [[ " $EIGEN " == *" $HIT "* ]] && continue
    AAN+="  comm $pat -> $HIT  $(tr '\0' ' ' < "/proc/$HIT/cmdline" 2>/dev/null | cut -c1-80)"$'\n'
  done
done
if [[ -n "$AAN" ]]; then fout "de bench is NIET vrij."; printf '%s' "$AAN"; exit 3; fi
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

game_pid() { local p c; for p in $(pgrep -x VACUUM.exe 2>/dev/null); do c=$(tr '\0' ' ' < "/proc/$p/cmdline" 2>/dev/null); [[ "$c" == *"LuigisMansion"* ]] && { echo "$p"; return; }; done; }
veeg() {
  local p c
  for p in $(pgrep -x VACUUM.exe 2>/dev/null); do c=$(tr '\0' ' ' < "/proc/$p/cmdline" 2>/dev/null); [[ "$c" == *"LuigisMansion"* ]] && kill -9 "$p" 2>/dev/null; done
  pkill -9 -f "OpenParrotLoader64.exe.*LuigisMansion" 2>/dev/null
  pkill -9 -f 'rg-jvsbridge' 2>/dev/null; pkill -9 -f 'pipehelper.exe' 2>/dev/null
  sleep 1; WINEPREFIX="$PFX" "$WINEBIN/wineserver" -k 2>/dev/null; sleep 1
}

doe_run() {
  local label="$1" variant="$2" out="$UIT/$1"
  local bedoeld; bedoeld=$(sha256sum "$variant" | awk '{print $1}')
  if [[ "$variant" != "$KERN" ]]; then
    [[ -f "$BEWAARD" ]] || cp -f "$KERN" "$BEWAARD"
    cp -f "$variant" "$KERN"
    [[ "$(sha256sum "$KERN" | awk '{print $1}')" == "$bedoeld" ]] || { fout "verwisselen mislukt"; herstel_kern; return 1; }
  fi
  zeg "run $label -- kern sha256 ${bedoeld:0:12}  (dump op ${MOMENT}s na eerste verschijning)"

  set -m
  ( cd "$REPO" && env DISPLAY="${DISPLAY:-}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}" \
      XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" WINEDEBUG="-all" \
      timeout "$MAX" python3 scripts/launch-direct.py LuigisMansion --run ) >"$out.log" 2>&1 &
  local pgid=$!
  set +m

  local t=0 gp gpid="" t0="" 
  while true; do
    gp=$(game_pid)
    if [[ -n "$gp" && -z "$gpid" ]]; then gpid="$gp"; t0=$t; zeg "spelproces $gp verschenen op t=${t}s"; fi
    if [[ -n "$gpid" && -n "$t0" ]]; then
      if awk -v a="$t" -v b="$t0" -v m="$MOMENT" 'BEGIN{exit !((a-b)>=m)}'; then break; fi
    fi
    kill -0 "$pgid" 2>/dev/null || { let_op "de run eindigde voor het dumpmoment"; break; }
    sleep 0.25; t=$(awk -v a="$t" 'BEGIN{print a+0.25}')
  done

  if [[ -n "$gpid" && -d "/proc/$gpid" ]]; then
    zeg "levenscheck + dump op pid $gpid"
    echo "$SUDOPW" | sudo -S python3 "$HIER/beeldcheck.py" "$gpid" >"$out.levenscheck.txt" 2>&1
    grep -aE 'Threads|CPU|OORDEEL|VASTGELOPEN|NOG NIET|Lijkt een levend|entrypoint' "$out.levenscheck.txt" | sed 's/^/    /'
    echo "$SUDOPW" | sudo -S python3 "$HIER/b0-geheugen-dump.py" "$gpid" "$UIT" "$label" >"$out.dump.txt" 2>&1
    cat "$out.dump.txt" | sed 's/^/    /'
  else
    let_op "geen levend spelproces op het dumpmoment -- geen dump"
    echo "geen proces" > "$out.dump.txt"
  fi

  kill -9 -"$pgid" 2>/dev/null; wait "$pgid" 2>/dev/null
  [[ "$variant" != "$KERN" ]] && herstel_kern
  veeg
  grep -a -oE 'kern \(runtime\): .* sha256 [0-9a-f]+' "$out.log" | head -1 | sed 's/.*sha256 //' > "$out.werkelijk-sha"
}

doe_run "gesloten" "$KERN"
doe_run "onze" "$OPC"

zeg "vergelijking"
{
  echo "kern werkelijk gebruikt:"
  for r in gesloten onze; do printf '  %-10s %s\n' "$r" "$(cat "$UIT/$r.werkelijk-sha" 2>/dev/null)"; done
  echo
  python3 - "$UIT" <<'PY'
import sys, os
uit = sys.argv[1]
def laad(tag):
    p = os.path.join(uit, "image-%s.bin" % tag)
    return open(p, "rb").read() if os.path.exists(p) else None
a, b = laad("gesloten"), laad("onze")
if a is None or b is None:
    print("  geen twee dumps om te vergelijken"); raise SystemExit
print("  dumpgroottes: gesloten %d B, onze %d B" % (len(a), len(b)))
n = min(len(a), len(b))
# bereiken van verschil, in blokken van 64 KB
BLOK = 0x10000
vers = []
i = 0
while i < n:
    blok_a = a[i:i+BLOK]; blok_b = b[i:i+BLOK]
    if blok_a != blok_b:
        d = sum(1 for x, y in zip(blok_a, blok_b) if x != y)
        vers.append((i, d, len(blok_a)))
    i += BLOK
if not vers:
    print("  GEEN VERSCHIL in de eerste %d B (%.1f MB): op dezelfde adressen staat onder beide" % (n, n/1048576.0))
    print("  kernen exact dezelfde bytes. Bewijs dat er gekeken is: het bereik is ImageBase..ImageBase+SizeOfImage")
    print("  en beide dumps zijn %d B, met dezelfde geldigheidsha256 hierboven." % n)
else:
    tot = sum(d for _, d, _ in vers)
    print("  %d blokken van 64 KB verschillen; %d van %d bytes anders (%.2f%%)" % (len(vers), tot, n, 100.0*tot/n))
    print("  eerste 25 blokken met verschil (offset t.o.v. ImageBase, aantal bytes):")
    for off, d, ln in vers[:25]:
        print("    0x%09x  %6d van %d" % (0x140000000 + off, d, ln))
    print("  laatste blok: 0x%09x" % (0x140000000 + vers[-1][0]))
PY
} | tee "$UIT/vergelijking.txt"

zeg "alles staat in $UIT"

