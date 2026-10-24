#!/bin/bash
# ============================================================================
#  A/B: does the closed core satisfy amPlatformGetPlatformInformation()?
#
#  PREPARED, NOT RUN.  Without --run this script only prints the plan and the
#  expectations and exits.  See AB-PLATFORMCHECK.md for the reasoning.
#
#  It starts the AM DAEMON ONLY.  No game, no loader-driven game, no kern in the
#  game, no display, no input.  Private copies of folder and prefix.  Hard cap
#  90 s per arm.  Arm D touches the NAS and therefore needs --arm-d explicitly.
# ============================================================================
set -u

RUN=0; ARM_D=0
for a in "$@"; do
  case "$a" in
    --run) RUN=1 ;;
    --arm-d) ARM_D=1 ;;
    *) echo "onbekende optie: $a"; exit 2 ;;
  esac
done

BASE="$HOME/rg-hotdsd-ab"
VIEW="$BASE/view"                       # full tree shape: App/Hodzero/WindowsNoEditor/...
BARE="$BASE/bare/AMDaemon"              # meting-1 shape: only the AMDaemon folder
P="$BASE/prefix"
SRC="/home/deck/mnt/spidernas_emulation/Teknoparrot/House of the Dead Scarlet Dawn"
REAL="$HOME/.local/share/ravengun/wine/proton-11.0-2/bin/wine"
WS="$(dirname "$REAL")/wineserver"
NASDAEMON="$SRC/App/Hodzero/WindowsNoEditor/AMDaemon"
VIEWDAEMON="$VIEW/App/Hodzero/WindowsNoEditor/AMDaemon"

# ---- locate the loader and the core without hard-coding a version -----------
LOADER=$(ls -1 "$HOME"/.local/share/ravengun/runtime/TeknoParrotUi-*/OpenParrotx64/OpenParrotLoader64.exe 2>/dev/null | tail -1)
# the loader takes the core path WITHOUT the .dll suffix (as on 2026-10-11)
CORE=$(ls -1 "$HOME"/.local/share/ravengun/runtime/TeknoParrotUi-*/TeknoParrot/TeknoParrot64 2>/dev/null | tail -1)
if [ -z "$CORE" ]; then
  CORE=$(ls -1 "$HOME"/.local/share/ravengun/runtime/TeknoParrotUi-*/TeknoParrot/TeknoParrot64.dll 2>/dev/null | tail -1)
  CORE="${CORE%.dll}"
fi

cat <<'PLAN'
============================================================================
 VERWACHTINGEN VOORAF  (eerst lezen, dan pas draaien)
============================================================================
 Arm A  lader + kern + daemon, view met dezelfde boomvorm
        verwacht: "Process state: setup Game Mode" / "Error 0" / "Process state: Advertise"
                  -> reproduceert de drie runs van 2026-10-11
 Arm B  dezelfde view, daemon ZONDER kern
        verwacht: sterft na 1 s met amPlatformGetPlatformInformation()
 Arm C  alleen de map AMDaemon/ (de opzet van meting 1), zonder kern
        verwacht: sterft na 1 s -- dit is de al gemeten nulmeting
 Arm D  (alleen met --arm-d) de exacte 10-11-opdracht, boom op de NAS
        verwacht: Advertise.  Zo niet, dan is er sinds 10-11 iets veranderd

 Als A Advertise geeft en B sterft  -> de kern is de bevrediger (hoofdverwachting)
 Als A en B beide Advertise geven   -> de boomvorm was het probleem, niet de kern
 Als A sterft en D Advertise geeft  -> er is sinds 10-11 iets anders veranderd
============================================================================
PLAN

echo "gevonden lader : ${LOADER:-<NIET GEVONDEN>}"
echo "gevonden kern  : ${CORE:-<NIET GEVONDEN>}"
echo

if [ "$RUN" -eq 0 ]; then
  echo "DRY RUN -- er is niets gestart.  Draai met --run (en eventueel --arm-d)."
  exit 0
fi

[ -n "$LOADER" ] && [ -n "$CORE" ] || { echo "lader of kern niet gevonden; arm A kan niet"; exit 1; }

# ------------------------------------------------------------------ prepare
if [ ! -d "$VIEW" ] || [ ! -d "$BARE" ] || [ ! -d "$P" ]; then
  echo "### voorbereiden (view met boomvorm, kale kopie, prefix-kopie)"
  rm -rf "$BASE"; mkdir -p "$VIEW/App/Hodzero/WindowsNoEditor" "$BARE" "$P"
  rsync -a --copy-links "$NASDAEMON/" "$VIEWDAEMON/"            # boomvorm
  rsync -a --copy-links "$NASDAEMON/" "$BARE/"                  # meting-1 vorm
  rsync -a --copy-links "$SRC/App/Hodzero/WindowsNoEditor/Hodzero/Binaries/Win64" \
        "$VIEW/App/Hodzero/WindowsNoEditor/Hodzero/Binaries/Win64/" 2>/dev/null
  rsync -a --copy-links "$SRC/App/Hodzero/WindowsNoEditor/Engine" \
        "$VIEW/App/Hodzero/WindowsNoEditor/Engine/" 2>/dev/null
  rsync -a "$HOME/.local/share/ravengun/prefixes/shared/wine/" "$P/"
  echo "   view   : $(du -sh "$VIEW" | cut -f1)   kale kopie: $(du -sh "$BARE" | cut -f1)   prefix: $(du -sh "$P" | cut -f1)"
  echo "   symlinks in de view: $(find "$VIEW" -type l | wc -l) (moet 0 zijn)"
  echo
fi

runlog() { echo "$1" > "$BASE/result-$2.txt"; }

# ------------------------------------------------------------------ arm A
echo "##### ARM A -- lader + kern + daemon (boomvorm)"
cd "$VIEWDAEMON"
timeout 90 env WINEPREFIX="$P" env -u DISPLAY -u WAYLAND_DISPLAY WINEDEBUG=-all \
  "$REAL" "$LOADER" "Z:$(echo "$CORE" | tr '/' '\\')" \
  "Z:$(echo "$VIEWDAEMON/amdaemon.exe" | tr '/' '\\')" > "$BASE/armA.txt" 2>&1
echo "   exit=$?"
grep -aE 'Process state|Advertise|Error|amdaemonExe|Runtime exception' "$BASE/armA.txt" | head -12 | sed 's/^/      /'
grep -aq 'Advertise' "$BASE/armA.txt" && runlog PASS A || runlog FAIL A
WINEPREFIX="$P" "$WS" -k 2>/dev/null; sleep 3
echo

# ------------------------------------------------------------------ arm B
echo "##### ARM B -- dezelfde view, daemon zonder kern"
timeout 90 env WINEPREFIX="$P" env -u DISPLAY -u WAYLAND_DISPLAY WINEDEBUG=-all \
  "$REAL" amdaemon.exe -c config.json -l "$VIEWDAEMON/armB.log" > "$BASE/armB.txt" 2>&1
echo "   exit=$?"
sed 's/^/      | /' "$VIEWDAEMON/armB.log" 2>/dev/null | head -8
if grep -aq 'Advertise' "$BASE/armB.txt" "$VIEWDAEMON/armB.log" 2>/dev/null; then runlog PASS B; else runlog FAIL B; fi
WINEPREFIX="$P" "$WS" -k 2>/dev/null; sleep 3
echo

# ------------------------------------------------------------------ arm C
echo "##### ARM C -- kale AMDaemon-map (de opzet van meting 1), zonder kern"
cd "$BARE"
timeout 90 env WINEPREFIX="$P" env -u DISPLAY -u WAYLAND_DISPLAY WINEDEBUG=-all \
  "$REAL" amdaemon.exe -c config.json -l "$BARE/armC.log" > "$BASE/armC.txt" 2>&1
echo "   exit=$?"
sed 's/^/      | /' "$BARE/armC.log" 2>/dev/null | head -8
if grep -aq 'Advertise' "$BASE/armC.txt" "$BARE/armC.log" 2>/dev/null; then runlog PASS C; else runlog FAIL C; fi
WINEPREFIX="$P" "$WS" -k 2>/dev/null; sleep 3
echo

# ------------------------------------------------------------------ arm D
if [ "$ARM_D" -eq 1 ]; then
  echo "##### ARM D -- exact de 10-11-opdracht, boom op de NAS  (RAAKT DE NAS AAN)"
  cd "$NASDAEMON"
  timeout 90 env WINEPREFIX="$P" env -u DISPLAY -u WAYLAND_DISPLAY WINEDEBUG=-all \
    "$REAL" "$LOADER" "Z:$(echo "$CORE" | tr '/' '\\')" \
    "Z:$(echo "$NASDAEMON/amdaemon.exe" | tr '/' '\\')" > "$BASE/armD.txt" 2>&1
  echo "   exit=$?"
  grep -aE 'Process state|Advertise|amdaemonExe|Runtime' "$BASE/armD.txt" | head -10 | sed 's/^/      /'
  grep -aq 'Advertise' "$BASE/armD.txt" && runlog PASS D || runlog FAIL D
  WINEPREFIX="$P" "$WS" -k 2>/dev/null; sleep 3
else
  echo "##### ARM D -- overgeslagen (geef --arm-d om de NAS aan te raken)"
fi
echo

# ------------------------------------------------------------------ verdict
echo "============================================================================
 VERDICT
============================================================================"
for a in A B C D; do
  if [ -f "$BASE/result-$a.txt" ]; then
    printf "   arm %s : %s\n" "$a" "$(cat "$BASE/result-$a.txt")"
  else
    printf "   arm %s : niet gedraaid\n" "$a"
  fi
done
if [ -f "$BASE/result-A.txt" ] && grep -q PASS "$BASE/result-A.txt"; then
  if [ -f "$BASE/result-B.txt" ] && grep -q FAIL "$BASE/result-B.txt"; then
    echo "   -> DE KERN IS DE BEVREDIGER.  De platformcontrole moet in onze eigen kern worden nagebouwd."
  elif [ -f "$BASE/result-B.txt" ] && grep -q PASS "$BASE/result-B.txt"; then
    echo "   -> DE BOOMVORM WAS HET PROBLEEM, niet de kern.  Meting 1 was misleidend door de kale kopie."
  fi
fi
echo

# ------------------------------------------------------------------ cleanup
echo "##### OPRUIMEN (alleen onze eigen prefix-kopieen)"
WINEPREFIX="$P" "$WS" -k 2>/dev/null; sleep 3
left=""
for pid in $(pgrep -x wineserver; pgrep -x winedevice.exe; pgrep -f 'amdaemon.exe'); do
  pref=$(tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | grep -m1 '^WINEPREFIX=' | cut -d= -f2-)
  [ "$pref" = "$P" ] && left="$left $pid"
done
[ -z "$left" ] && echo "   niets van ons over" || echo "   NOG ACTIEF:$left"
echo "   NAS onaangeroerd?"
find "$SRC" -newermt "-30 minutes" 2>/dev/null | head -3
echo "   (leeg = onaangeroerd)"
echo "   resultaten: $BASE/arm*.txt en $BASE/result-*.txt"
echo "   opruimen:   rm -rf $BASE"
