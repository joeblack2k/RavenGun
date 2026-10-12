#!/usr/bin/env bash
# b0-vijfmin.sh -- de 5-minutenproef voor Luigi's Mansion op ONZE lader.
#
# DOEL (eigenaar): het spel draait 5 MINUTEN op onze eigen lader. Tot nu toe: 288 s met de
# GESLOTEN kern, 6,3 s met onze kern. Deze proef is de meetlat.
#
# WAT HET DOET
#   * de gewone werkende route: `launch-direct.py LuigisMansion --run` (die zet prefix, werkmap
#     van de lader, hulpketen en omgeving goed), headless gamescope zodat het scherm vrij blijft;
#   * de kern-DLL die je opgeeft (standaard onze huidige build) wordt tijdelijk op zijn plaats
#     gezet en daarna GEGARANDEERD teruggezet, met sha256-controle;
#   * elke 30 s game-tijd een regel: game-tijd, leeft het, threads, entry-point-staat
#     (trampoline of teruggezet), .text-vulling, en hoeveel bestanden van het spel openstaan;
#   * harde limiet standaard 480 s, stopgrens 300 s game-tijd;
#   * NAS-schrijfcontrole over het hele venster (mtime/hash voor en na);
#   * per run de kern-sha256 zoals launch-direct hem ZELF in zijn log schrijft;
#   * opruimen: procesgroep kill -9, resten wegvegen, runtime-kern terug op f5c7532d.
#
# SYSTEEM-UNIT: dit script zet zichzelf in een systemd-user-unit als het niet al onder systemd
# loopt, zodat het een ssh-sessie overleeft. Volgen: journalctl --user -u <unit> -f
#
# SUCCESCRITERIA (vooraf, zie ook B0-VIJFMIN.md)
#   1. 300 s game-tijd gehaald (stopgrens bereikt met een levend proces)
#   2. entry point TERUGGEZET op het stopmoment (eb 08), niet de trampoline
#   3. .text gevuld (ruim onder 100% nul; de gesloten kern zit op 11,2%)
#   4. threads ruim boven 1 (de gesloten kern: 15 op 10 s, 21 op 12 s)
#   5. het spel heeft zijn eigen databestanden gelezen
#   6. geen onverwachte schrijfactie naar de NAS
#
# GEBRUIK
#   bash b0-vijfmin.sh                                   # onze huidige kern
#   RG_B0_KERN_DLL=/pad/OpenParrot64.dll bash b0-vijfmin.sh   # de nieuwe kern van de laderbouwer
#   RG_B0_NO_SYSTEMD=1 bash b0-vijfmin.sh                # direct, zonder unit (kort debuggen)
set -u
HIER="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

REPO="${RG_REPO:-$HOME/projects/ravengun}"
TP="${RAVENGUN_TP_DIR:-$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64}"
KERN="$TP/TeknoParrot/TeknoParrot64.dll"
KERN_ORIG_SHA_EXPECTED="f5c7532d6e63848564a4b9233d30706793066d94d3bbafafe45bf72c2b1d6e28"
NIEUW="${RG_B0_KERN_DLL:-$HOME/.local/share/ravengun/openparrot/current/OpenParrotx64/OpenParrot64.dll}"
PFX="${RG_WINEPREFIX:-$HOME/.local/share/ravengun/prefixes/shared/wine}"
WINEBIN="${RG_WINE_BIN:-$HOME/.local/share/ravengun/wine/proton-11.0-2/bin}"
NAS="${RG_NAS_GAME:-}"
[ -n "$NAS" ] || NAS="/home/deck/mnt/spidernas_emulation/Teknoparrot/Luigi's Mansion Arcade"
STOP="${RG_B0_STOP:-300}"        # stopgrens in s game-tijd
HARD="${RG_B0_HARD:-480}"        # harde limiet in s muurtijd
SUDOPW="${RG_SUDO_PW:-deck}"
UIT="${RG_B0_UIT:-/tmp/b0-vijfmin-$(date +%Y%m%d-%H%M%S)}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

zeg()  { printf '\033[1m==\033[0m %s\n' "$*"; }
fout() { printf '\033[31mFOUT\033[0m %s\n' "$*" >&2; }

# ---------------------------------------------------------------- 0. systeem-unit
if [[ -z "${INVOCATION_ID:-}" && "${RG_B0_NO_SYSTEMD:-0}" != 1 ]] && command -v systemd-run >/dev/null 2>&1; then
  UNIT="ravengun-b0-vijfmin-$(date +%H%M%S)"
  echo "Ik zet mezelf in de systemd-user-unit $UNIT -- overleeft een ssh-sessie."
  echo "Volgen:  journalctl --user -u $UNIT -f"
  echo "Uitvoer: $UIT"
  exec systemd-run --user --unit="$UNIT" --collect --same-dir \
       --setenv=RG_B0_UIT="$UIT" --setenv=RG_B0_KERN_DLL="$NIEUW" \
       --setenv=RG_B0_STOP="$STOP" --setenv=RG_B0_HARD="$HARD" \
       --setenv=RG_B0_IN_UNIT=1 --setenv=RG_WINEPREFIX="$PFX" \
       /bin/bash "$0" "$@"
fi

zeg "bench-controle"
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
[[ -f "$NIEUW" ]] || { fout "kern-DLL niet gevonden: $NIEUW"; exit 1; }
mkdir -p "$UIT"

ORIG_SHA=$(sha256sum "$KERN" | awk '{print $1}')
NIEUW_SHA=$(sha256sum "$NIEUW" | awk '{print $1}')
BEWAARD="$KERN.b0-bewaard"
herstel_kern() {
  if [[ -f "$BEWAARD" ]]; then rm -f "$KERN"; mv -f "$BEWAARD" "$KERN"; fi
  local nu; nu=$(sha256sum "$KERN" | awk '{print $1}')
  if [[ "$nu" != "$KERN_ORIG_SHA_EXPECTED" ]]; then
    fout "runtime-kern is NIET terug op het origineel: $nu -- verwacht $KERN_ORIG_SHA_EXPECTED"
  else
    echo "[b0] runtime-kern terug op $nu"
  fi
}
GSPID=""
opruimen() {
  herstel_kern
  if [[ -n "$GSPID" ]]; then kill -9 -"$GSPID" 2>/dev/null; sleep 0.5; pkill -9 -f 'gamescope --backend headless' 2>/dev/null; fi
}
trap opruimen EXIT

# ---------------------------------------------------------------- 1. NAS-schrijfcontrole VOOR
nas_snapshot() {
  [[ -d "$NAS" ]] || { echo "geen NAS-map"; return; }
  find "$NAS" -type f -newermt "-1 day" -printf '%T@ %s %p\n' 2>/dev/null | sort | head -50
  echo "--- hashes van de bekende schrijfdoelen ---"
  for f in "$NAS/exe/x64/teknoparrot.ini" "$NAS/Log/play_log.csv" "$NAS/Log/play_number.dat" "$NAS/Ram/CreditConfig.bin"; do
    [[ -e "$f" ]] && sha256sum "$f"
  done
}
nas_snapshot > "$UIT/nas-voor.txt"

# ---------------------------------------------------------------- 2. headless display
zeg "headless gamescope starten"
rm -f "$UIT/gamescope.log"
setsid gamescope --backend headless -W 1920 -H 1080 -- sleep 3600 >"$UIT/gamescope.log" 2>&1 </dev/null &
GSPID=$!
DISP=""
for _ in $(seq 1 40); do DISP=$(grep -o 'Starting Xwayland on :[0-9]*' "$UIT/gamescope.log" 2>/dev/null | tail -1 | grep -o '[0-9]*$'); [[ -n "$DISP" ]] && break; sleep 1; done
[[ -n "$DISP" ]] || { fout "gamescope kwam niet op"; exit 1; }
export DISPLAY=":$DISP" WAYLAND_DISPLAY=gamescope-0
zeg "DISPLAY=:$DISP"

# ---------------------------------------------------------------- 3. kern verwisselen
if [[ "$NIEUW_SHA" != "$ORIG_SHA" ]]; then
  cp -f "$KERN" "$BEWAARD"
  cp -f "$NIEUW" "$KERN"
  [[ "$(sha256sum "$KERN" | awk '{print $1}')" == "$NIEUW_SHA" ]] || { fout "verwisselen mislukt"; herstel_kern; exit 1; }
fi
{
  echo "datum            $(date -Is)"
  echo "route            launch-direct.py LuigisMansion --run, headless gamescope"
  echo "kern-DLL         $NIEUW"
  echo "kern sha256      $NIEUW_SHA"
  echo "runtime VOOR     $ORIG_SHA"
  echo "stopgrens        ${STOP}s game-tijd;  harde limiet ${HARD}s"
  echo "DISPLAY          $DISPLAY"
  echo "WINEDEBUG        -all   GEEN +file -- die trace vertraagt het proces en verandert de levensduur"
  echo "eigen-bestand    gemeten via /proc/<pid>/fd naar de Data-mappen, niet via de trace"
} > "$UIT/opzet.txt"
cat "$UIT/opzet.txt"

# ---------------------------------------------------------------- 4. de run + de bemonstering
zeg "run starten en bemonsteren, elke 30 s game-tijd"
set -m
( cd "$REPO" && env DISPLAY="${DISPLAY:-}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}" \
    XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" WINEDEBUG="-all" \
    timeout "$HARD" python3 scripts/launch-direct.py LuigisMansion --run ) >"$UIT/run.log" 2>&1 &
PGID=$!
set +m

echo "$SUDOPW" | sudo -S python3 "$HIER/b0-vijfmin-sampler.py" "$UIT" "vijfmin" "$STOP" "$HARD" 2>&1 | sed 's/^/  /'

kill -9 -"$PGID" 2>/dev/null; wait "$PGID" 2>/dev/null
RC=$?
# resten wegvegen
for p in $(pgrep -x VACUUM.exe 2>/dev/null); do
  c=$(tr '\0' ' ' < "/proc/$p/cmdline" 2>/dev/null); [[ "$c" == *"LuigisMansion"* ]] && kill -9 "$p" 2>/dev/null
done
pkill -9 -f "OpenParrotLoader64.exe.*LuigisMansion" 2>/dev/null
pkill -9 -f 'rg-jvsbridge' 2>/dev/null; pkill -9 -f 'pipehelper.exe' 2>/dev/null
sleep 1
WINEPREFIX="$PFX" "$WINEBIN/wineserver" -k 2>/dev/null
herstel_kern

# ---------------------------------------------------------------- 5. NAS-schrijfcontrole NA
nas_snapshot > "$UIT/nas-na.txt"
diff <(grep -v '^--- ' "$UIT/nas-voor.txt") <(grep -v '^--- ' "$UIT/nas-na.txt") > "$UIT/nas-verschil.txt" 2>&1 || true

# ---------------------------------------------------------------- 6. oordeel
WERKELIJK=$(grep -a -oE 'kern \(runtime\): .* sha256 [0-9a-f]+' "$UIT/run.log" | head -1 | sed 's/.*sha256 //')
zeg "oordeel"
{
  echo "kern volgens launch-direct: ${WERKELIJK:-niet gevonden}"
  echo "kern die wij bedoelden    : $NIEUW_SHA"
  echo
  cat "$UIT/vijfmin-vijfmin.txt" 2>/dev/null
  echo
  echo "--- NAS-verschil over het venster ---"
  if [ -s "$UIT/nas-verschil.txt" ]; then cat "$UIT/nas-verschil.txt"; else echo "  geen verschil in de bekende schrijfdoelen"; fi
  echo
  echo "--- SUCCESCRITERIA ---"
  LAATSTE=$(grep -a '^t=' "$UIT/vijfmin-vijfmin.txt" 2>/dev/null | tail -1)
  echo "1. 300 s game-tijd gehaald         : $(grep -ac 'STOPGRENS' "$UIT/vijfmin-vijfmin.txt" 2>/dev/null)"
  echo "2. entry point teruggezet          : $(echo "$LAATSTE" | grep -c TERUGGEZET)"
  echo "3. laatste regel                   : $LAATSTE"
  echo "4. eigen databestanden             : $(grep -a 'TOTAAL eigen databestanden' "$UIT/vijfmin-vijfmin.txt" 2>/dev/null)"
} | tee "$UIT/oordeel.txt"

zeg "alles staat in $UIT"
