#!/usr/bin/env bash
# b0-com1.sh -- de COM1-vraag met de werkende route, en de A/B op patch 0005.
#
# WAAROM
#   Patch 0005 blokkeert elke COM-naam met FILE_NOT_FOUND, en dat is nagebouwd van een bewering
#   over de gesloten kern ("BLOCKING LUIGI COM PORT") die in de binary van 1.0.0.3761 NIET terug
#   te vinden is: geen Luigi-naam en geen van de drie CRC-vormen. De premisse is dus onbewezen, en
#   de patch doet misschien iets dat de gesloten kern helemaal niet doet. Daarom: onze kern ZONDER
#   0005 naast onze kern MET 0005, allebei met een pty op COM1, en kijken of het spel de poort
#   aanraakt.
#
# OPZET (alles via de werkende route, zie B0-LEGE-DLL.md Â§4)
#   1. headless gamescope (het scherm van de eigenaar blijft vrij)
#   2. de wineserver van de prefix VOORVERWARMEN -- anders zet Wine dosdevices/com1..com32 opnieuw
#      op /dev/ttyS* en is onze link weg voordat de game start
#   3. pty op dosdevices/com1, elke byte met tijdstempel en hex in een log (geen antwoordmodus)
#   4. de kern-DLL verwisselen naar de gekozen variant
#   5. `launch-direct.py LuigisMansion --run` met een harde tijdslimiet
#   6. alles terugzetten: kern-DLL (met sha256-controle tegen wat launch-direct ZELF logt),
#      dosdevices/com1, de pty, de gamescope, en resten wegvegen
#
# UITKOMSTEN
#   * de game raakt COM1 NIET aan   -> COM is de hefboom niet; 0005 is een dood spoor
#   * de game OPENT COM1 en wacht    -> dan is onze blokkade precies het verkeerde antwoord, en
#                                       moet het antwoord de I/O-kaart zijn (meten, niet omzeilen)
#   * de game OPENT COM1 en schrijft zonder te wachten -> openen is genoeg, een doorgeef-pty is
#                                       de hele oplossing
#
# GEBRUIK
#   bash b0-com1.sh zonder0005     # onze kern zonder 0005
#   bash b0-com1.sh met0005        # onze kern met 0005 (de huidige build)
#   bash b0-com1.sh beide          # allebei, na elkaar
set -u
HIER="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

REPO="${RG_REPO:-$HOME/projects/ravengun}"
TP="${RAVENGUN_TP_DIR:-$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64}"
KERN="$TP/TeknoParrot/TeknoParrot64.dll"
OPC="$HOME/.local/share/ravengun/openparrot/current/OpenParrotx64/OpenParrot64.dll"
B0="${B0_DIR:-$HOME/.local/share/ravengun/b0}"
ZONDER="$B0/core-zonder-0005/OpenParrotx64/OpenParrot64.dll"
PFX="${RG_WINEPREFIX:-$HOME/.local/share/ravengun/prefixes/shared/wine}"
WINEBIN="${RG_WINE_BIN:-$HOME/.local/share/ravengun/wine/proton-11.0-2/bin}"
MAX="${RG_B0_MAX:-45}"
UIT="${RG_B0_UIT:-/tmp/b0-com1-$(date +%Y%m%d-%H%M%S)}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
LINK="$UIT/com1-pty"

zeg()  { printf '\033[1m==\033[0m %s\n' "$*"; }
let_op(){ printf '\033[33m!!\033[0m %s\n' "$*"; }
fout() { printf '\033[31mFOUT\033[0m %s\n' "$*" >&2; }

# ---------------------------------------------------------------- 0. veiligheid
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
    AAN+="  comm $pat -> $HIT  $(tr '\0' ' ' < "/proc/$HIT/cmdline" 2>/dev/null | cut -c1-90)"$'\n'
  done
done
if [[ -n "$AAN" ]]; then fout "de bench is NIET vrij; deze meting start niets."; printf '%s' "$AAN"; exit 3; fi
zeg "bench vrij (eigen procesboom $EIGEN uitgesloten)"
[[ -f "$ZONDER" ]] || { fout "kern zonder 0005 ontbreekt: $ZONDER (bouwen: b0-bouw-kern.sh)"; exit 1; }
[[ -f "$OPC" ]]    || { fout "onze kern met 0005 ontbreekt: $OPC"; exit 1; }
[[ -d "$PFX/dosdevices" ]] || { fout "prefix onbruikbaar: $PFX"; exit 1; }
mkdir -p "$UIT"

ORIG_SHA=$(sha256sum "$KERN" | awk '{print $1}')
BEWAARD="$KERN.b0-bewaard"
herstel_kern() {
  if [[ -f "$BEWAARD" ]]; then rm -f "$KERN"; mv -f "$BEWAARD" "$KERN"; fi
  local nu; nu=$(sha256sum "$KERN" | awk '{print $1}')
  [[ "$nu" == "$ORIG_SHA" ]] || fout "kern-DLL wijkt af: $nu (verwacht $ORIG_SHA)"
}
ORIG_COM1=""; [[ -L "$PFX/dosdevices/com1" ]] && ORIG_COM1=$(readlink "$PFX/dosdevices/com1")
herstel_com1() {
  if [[ -L "$PFX/dosdevices/com1" && "$(readlink "$PFX/dosdevices/com1")" == "$LINK" ]]; then
    if [[ -n "$ORIG_COM1" ]]; then ln -sfn "$ORIG_COM1" "$PFX/dosdevices/com1"; else rm -f "$PFX/dosdevices/com1"; fi
  fi
}
PTYPID=""; GSPID=""
opruimen() {
  herstel_kern; herstel_com1
  [[ -n "$PTYPID" ]] && kill "$PTYPID" 2>/dev/null
  if [[ -n "$GSPID" ]]; then kill -9 -"$GSPID" 2>/dev/null; sleep 0.5; pkill -9 -f 'gamescope --backend headless' 2>/dev/null; fi
}
trap opruimen EXIT

# ---------------------------------------------------------------- 1. headless display
zeg "headless gamescope starten"
rm -f "$UIT/gamescope.log"
setsid gamescope --backend headless -W 1920 -H 1080 -- sleep 3600 >"$UIT/gamescope.log" 2>&1 </dev/null &
GSPID=$!
DISP=""
for _ in $(seq 1 40); do
  DISP=$(grep -o 'Starting Xwayland on :[0-9]*' "$UIT/gamescope.log" 2>/dev/null | tail -1 | grep -o '[0-9]*$')
  [[ -n "$DISP" ]] && break; sleep 1
done
[[ -n "$DISP" ]] || { fout "gamescope kwam niet op"; exit 1; }
export DISPLAY=":$DISP" WAYLAND_DISPLAY=gamescope-0
zeg "DISPLAY=:$DISP"

# ---------------------------------------------------------------- 2. prefix voorverwarmen
# Zonder dit zet Wine com1..com32 opnieuw op /dev/ttyS* zodra zijn wineserver start, en dan is
# onze pty-link weg voordat de game bestaat.
zeg "wineserver van de prefix voorverwarmen (anders gooit Wine com1 terug naar /dev/ttyS0)"
WINEPREFIX="$PFX" WINEDEBUG=-all "$WINEBIN/wineserver" -p 2>/dev/null || true
WINEPREFIX="$PFX" WINEDEBUG=-all timeout 120 "$WINEBIN/wine" cmd /c exit >/dev/null 2>&1 || true
sleep 1
zeg "dosdevices/com1 stond op: ${ORIG_COM1:-<niets>}"

# ---------------------------------------------------------------- 3. hulpfuncties
game_pid() { local p c; for p in $(pgrep -x VACUUM.exe 2>/dev/null); do c=$(tr '\0' ' ' < "/proc/$p/cmdline" 2>/dev/null); [[ "$c" == *"LuigisMansion"* ]] && { echo "$p"; return; }; done; }
threads_van() { local p="$1"; [[ -d "/proc/$p" ]] && ls "/proc/$p/task" 2>/dev/null | wc -l || echo "weg"; }
veeg() {
  local p c
  for p in $(pgrep -x VACUUM.exe 2>/dev/null); do c=$(tr '\0' ' ' < "/proc/$p/cmdline" 2>/dev/null); [[ "$c" == *"LuigisMansion"* ]] && kill -9 "$p" 2>/dev/null; done
  pkill -9 -f "OpenParrotLoader64.exe.*LuigisMansion" 2>/dev/null
  pkill -9 -f 'rg-jvsbridge' 2>/dev/null; pkill -9 -f 'pipehelper.exe' 2>/dev/null
  sleep 1
  WINEPREFIX="$PFX" "$WINEBIN/wineserver" -k 2>/dev/null; sleep 1
}

# ---------------------------------------------------------------- 4. een run
doe_run() {
  local label="$1" variant="$2"
  local out="$UIT/$label"
  local log="$out.com1.log"
  local rc ld_sha bedoeld geldig="ja"
  bedoeld=$(sha256sum "$variant" | awk '{print $1}')

  [[ -f "$BEWAARD" ]] || cp -f "$KERN" "$BEWAARD"
  cp -f "$variant" "$KERN"
  [[ "$(sha256sum "$KERN" | awk '{print $1}')" == "$bedoeld" ]] || { fout "verwisselen mislukt"; herstel_kern; return 1; }

  zeg "run $label -- kern $(basename "$(dirname "$variant")")/$(basename "$variant")  sha256 ${bedoeld:0:12}"

  rm -f "$LINK" "$log"
  python3 "$HIER/com1-harness/luigi-com1-pty.py" --link "$LINK" --log "$log" \
     --max-seconds $((MAX + 30)) --banner "run=$label kern=$bedoeld" >"$out.pty.out" 2>&1 &
  PTYPID=$!
  for _ in $(seq 1 50); do [[ -L "$LINK" ]] && break; sleep 0.1; done
  [[ -L "$LINK" ]] || { fout "pty kwam niet op"; cat "$out.pty.out"; return 1; }
  ln -sfn "$LINK" "$PFX/dosdevices/com1"
  [[ "$(readlink "$PFX/dosdevices/com1")" == "$LINK" ]] || { fout "kon com1 niet omzetten"; return 1; }
  zeg "dosdevices/com1 -> $LINK -> $(readlink "$LINK")"

  {
    echo "run            $label"
    echo "kern-DLL       $variant"
    echo "bedoelde sha256 $bedoeld"
    echo "COM1           $PFX/dosdevices/com1 -> $LINK -> $(readlink "$LINK")"
    echo "commandoregel  cd $REPO && DISPLAY=$DISPLAY timeout $MAX python3 scripts/launch-direct.py LuigisMansion --run"
  } > "$out.cmdline"

  set -m
  ( cd "$REPO" && env DISPLAY="${DISPLAY:-}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}" \
      XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" WINEDEBUG="-all,+file" \
      timeout "$MAX" python3 scripts/launch-direct.py LuigisMansion --run ) >"$out.log" 2>&1 &
  local pgid=$!
  set +m
  local t=0 gp th gpid_eerste="" max_th=0 eerste_th="-"
  while true; do
    gp=$(game_pid)
    if [[ -n "$gp" ]]; then
      th=$(threads_van "$gp"); [[ -z "$gpid_eerste" ]] && { gpid_eerste="$gp"; eerste_th="$th"; }
      [[ "$th" =~ ^[0-9]+$ ]] && (( th > max_th )) && max_th=$th
    fi
    kill -0 "$pgid" 2>/dev/null || break
    sleep 0.25; t=$(awk -v a="$t" 'BEGIN{print a+0.25}')
  done
  wait "$pgid" 2>/dev/null; rc=$?
  sleep 1
  kill "$PTYPID" 2>/dev/null; wait "$PTYPID" 2>/dev/null || true; PTYPID=""
  herstel_kern; herstel_com1; veeg

  ld_sha=$(grep -a -oE 'kern \(runtime\): .* sha256 [0-9a-f]+' "$out.log" 2>/dev/null | head -1 | sed 's/.*sha256 //')
  [[ -n "$ld_sha" && "$ld_sha" != "$bedoeld" ]] && { geldig="NEE"; let_op "ONGELDIG: launch-direct gebruikte $ld_sha"; }

  local rx
  rx=$(grep -ac '<<< spel schrijft' "$log" 2>/dev/null); rx=${rx:-0}

  {
    echo "kern           $variant"
    echo "werkelijk gebruikt (uit launch-direct's log) ${ld_sha:-<niet gevonden>}"
    echo "GELDIG         $geldig"
    echo "exitcode       timeout/launch-direct = $rc   $(grep -aoE 'klaar \(exitcode [^)]*\)' "$out.log" | tail -1)"
    echo "looptijd       $(printf '%.1f' "$t") s (limiet ${MAX}s)"
    echo "spelproces     ${gpid_eerste:-nooit gezien}"
    echo "threads        bij eerste verschijning: $eerste_th   maximum: $max_th"
    echo
    echo "COM1: $rx keer iets van het spel ontvangen"
    if [[ "$rx" -gt 0 ]]; then
      echo "  HET SPEL HEEFT DE POORT AANGERAAKT. Eerste 20 blokken:"
      grep -a -E '<<< spel schrijft|>>> wij antwoorden' "$log" | head -20 | sed 's/^/    /'
      echo "  totaal van het spel: $(grep -aoE 'einde: [0-9]+ B' "$log" | head -1)"
    else
      echo "  De game heeft de poort NIET aangeraakt."
    fi
    echo
    echo "bestanden      $(grep -ac 'CreateFileW' "$out.log") CreateFileW-regels, $(grep -aoE 'CreateFileW L"[^"]*"' "$out.log" | sed 's/^CreateFileW L"//; s/"$//' | sort -u | grep -ciE 'Data(Global)?[\\/]') eigen Data/DataGlobal-paden"
    echo
    echo "--- laatste 10 regels ---"
    tail -10 "$out.log" | cut -c1-190
  } > "$out.verslag"
  sed 's/^/    /' "$out.verslag"
  echo "$rc" > "$out.exit"; echo "$geldig" > "$out.geldig"
}

# ---------------------------------------------------------------- 5. de runs
WAT=("$@"); [[ ${#WAT[@]} -eq 0 ]] && WAT=(beide)
for r in "${WAT[@]}"; do
  case "$r" in
    zonder0005) doe_run "com1-zonder-0005" "$ZONDER" ;;
    met0005)    doe_run "com1-met-0005"    "$OPC" ;;
    beide)      doe_run "com1-zonder-0005" "$ZONDER"; doe_run "com1-met-0005" "$OPC" ;;
    *) fout "onbekend: $r (zonder0005|met0005|beide)"; exit 2 ;;
  esac
done

zeg "oordeel"
{
  echo "kern sha256 na alle runs: $(sha256sum "$KERN" | awk '{print $1}')   bij de start: $ORIG_SHA"
  echo "dosdevices/com1 nu: $(readlink "$PFX/dosdevices/com1" 2>/dev/null)"
  echo
  for r in com1-zonder-0005 com1-met-0005; do
    [[ -f "$UIT/$r.exit" ]] || continue
    printf '%-18s exit=%-5s geldig=%-4s COM1-blokken=%s\n' "$r" "$(cat "$UIT/$r.exit")" "$(cat "$UIT/$r.geldig")" \
      "$(grep -ac '<<< spel schrijft' "$UIT/$r.com1.log" 2>/dev/null || echo 0)"
  done
  echo
  echo "vooraf opgeschreven: raakt de game de poort niet aan, dan is COM de hefboom niet en is 0005"
  echo "een dood spoor. Opent hij de poort en wacht hij, dan is onze blokkade het verkeerde antwoord"
  echo "en moet het antwoord de I/O-kaart zijn. Schrijft hij zonder te wachten, dan is openen genoeg."
} | tee "$UIT/oordeel.txt"
zeg "alles staat in $UIT"

