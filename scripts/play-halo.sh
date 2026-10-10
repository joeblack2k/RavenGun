#!/usr/bin/env bash
# Start Halo: Fireteam Raven via TeknoParrot met de Move als lightgun, in de juiste volgorde:
# daemon actief -> uitvoer wapenen (gun + toetsenbord bestaan) -> optioneel opnemen -> game starten.
# TeknoParrot opent de invoerapparaten alleen bij de gamestart; daarom eerst wapenen.
#
# Gebruik: scripts/play-halo.sh [--profile NAAM] [--record TAG] [--no-wait-move] [--direct|--tpui] [--log-full]
#          scripts/play-halo.sh --stop        (game stoppen, opname stoppen, uitvoer ontwapenen)
# --profile: TeknoParrot-profiel uit UserProfiles/ (standaard Halo; bv. JurassicPark, zie tp-add-game.py).
# --gamescope: laat TeknoParrotUI de game in gamescope draaien (TP_GAMESCOPE=1; schaalt een venster-game schermvullend,
#              werkt o.a. voor Terminator Salvation; staat daar ook in UserProfiles/Terminator.env).
# --route R: invoerroute afdwingen (evdev | tp-shm | unity-tcp | hid-board | jvs); standaard de keuze in de app
#            (~/.config/ravengun/io-routes.json) over packaging/io-routes.json (scripts/io-route.py). tp-shm: de
#            daemon schrijft zelf het gedeelde geheugen van de TeknoParrot-kern (zie scripts/tp-shm-swap.sh en docs/decisions.md).
#            hid-board: de daemon maakt het invoerbord van de game na als virtuele joystick (Transformers; hid-boards.json)
#            en zet dat apparaat in de Wine-prefix van het profiel op joystick-id 0 (scripts/wine-joyid.sh).
#            jvs: de daemon is het JVS-bord (Let's Go Safari): een pty die als COM-poort (dosdevices/comN) in de Wine-prefix van het
#            profiel staat (RG_WINEPREFIX in UserProfiles/<PROFIEL>.env) en de LD_PRELOAD-hulp voor GetCommModemStatus; bij --stop
#            wordt de koppeling opgeruimd (docs/procedures.md, "Let's Go Safari").
# --native: forceer de native route (scripts/play-native.sh, zonder Wine/BudgieLoader; profiel uit packaging/native-games.json), ook
#           als het profiel daar nog niet op enabled staat (experiment). Staat een profiel op enabled, dan is native de standaard
#           (net als de Unity-route); --teknoparrot dwingt dan de TeknoParrot-route af.
# --env K=V: extra omgevingsvariabele voor TeknoParrot/Wine/de game (herhaalbaar). Daarnaast wordt, indien
#            aanwezig, UserProfiles/<PROFIEL>.env gelezen (regels K=V, # = commentaar) vanuit de TeknoParrot-map.
# --direct: start de game zonder TeknoParrotUi (scripts/launch-direct.py; A10): het script start de lader van de runtime zelf,
#           met pipehelper.exe en het gedeelde geheugen, in dezelfde unit ravengun-tpui. De invoer komt dan altijd van de daemon
#           (route tp-shm; een profiel met route evdev schakelt over op tp-shm als er een indeling is). Staat in io-routes.json
#           "launch": "direct" bij het profiel, dan is dit de standaard; --tpui dwingt dan TeknoParrotUi af. Kan de directe start
#           niet (activering niet in de prefix, profiel niet ondersteund), dan valt het script terug op TeknoParrotUi (behalve
#           met --direct: dan stopt het met een melding).
# --log-full: (alleen direct) de kern schrijft ook naar zijn logbestand (TP_LOGTOFILE=1) en de regels over aanpassingen
#           komen in ~/.local/state/ravengun/a11/ (A11).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/scripts/rg-paths.sh"   # RG_BIN en RG_LIB: repo (build/) of installatie (<prefix>/bin en lib)
CTL="$RG_BIN/ravengunctl"
TP="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64"
WINE="$HOME/.local/share/ravengun/wine/proton-11.0-2/bin"
PREFIX="$HOME/.local/share/TeknoParrotUI/prefixes/shared/wine"
LOGDIR="$HOME/.local/state/ravengun/logs"
UNIT=ravengun-tpui
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
JVS_LINK="$XDG_RUNTIME_DIR/ravengun/jvs-com"            # symlink van de daemon naar de pty (jvs-route)
JVS_STATE="$XDG_RUNTIME_DIR/ravengun/jvs-prefix.state"  # welke prefix/poort wij gekoppeld hebben, voor het opruimen
SHIM="$RG_LIB/librg_comm_shim.so:$RG_LIB/lib32/librg_comm_shim.so"   # LD_PRELOAD-hulp, 64 en 32 bit (tools/wine-comm-shim/)

# Meldingen: in een terminal op stdout/stderr; gestart vanaf het bureaublad of Steam (geen terminal) ook als
# bureaubladmelding, zodat de speler ziet wat er gebeurt (bv. "druk op PS").
say()  { echo "$*"; [[ -t 1 ]] || notify-send -a RavenGun -t 8000 -i input-gaming "RavenGun" "$*" 2>/dev/null || true; }
fail() { echo "$*" >&2; [[ -t 2 ]] || notify-send -a RavenGun -t 20000 -i dialog-error "RavenGun" "$*" 2>/dev/null || true; exit 1; }

# Alle processen van een test leven in de cgroup van de transient user-unit ravengun-tpui (TeknoParrotUi, de
# Proton-/Wine-processen, wineserver, de game). Daardoor kun je ze per profiel herkennen en allemaal in een keer
# stoppen, zonder een andere TeknoParrot-sessie of wineserver van de speler aan te raken.
unit_pids() {
  local cg; cg=$(systemctl --user show -p ControlGroup --value "$UNIT.service" 2>/dev/null) || return 0
  [[ -n "$cg" && -r "/sys/fs/cgroup$cg/cgroup.procs" ]] && cat "/sys/fs/cgroup$cg/cgroup.procs" || true
}

# Procesnamen (comm: kleine letters, afgekapt op 15 tekens) die "de game draait" betekenen voor dit profiel:
# de loaders (BudgieLoader*, OpenParrotLoade*) plus de bestandsnamen uit ExecutableName, GamePath en GamePath2.
GAME_COMMS=""
build_game_comms() {
  local f="$TP/UserProfiles/$PROFILE.xml" v n
  GAME_COMMS="|budgieloader_x6|budgieloader.ex|openparrotloade|"
  # Native route: de game draait onder dit comm (bestandsnaam van de exe uit native-games.json, op 15 tekens afgekapt).
  [[ -n "${NATIVE_EXE:-}" ]] && { n="${NATIVE_EXE,,}"; GAME_COMMS+="${n:0:15}|"; }
  [[ -f "$f" ]] || return 0
  while IFS= read -r v; do
    IFS=';' read -ra parts <<< "$v"
    for n in "${parts[@]}"; do
      n="${n//\\//}"; n="${n##*/}"; n="${n,,}"; n="${n:0:15}"
      [[ -n "$n" ]] && GAME_COMMS+="$n|"
    done
  done < <(sed -n 's|.*<\(ExecutableName\|GamePath2\?\)>\(.*\)</\1>.*|\2|p' "$f")
}

# Wine zet bij het starten van een wineserver (wineboot/mountmgr) alle dosdevices/com1..com32 opnieuw naar /dev/ttyS*; een link die we
# eerder maakten is dan weg en de game opent /dev/ttyS3 (geen toegang). Daarom: eerst de wineserver van de prefix persistent starten en
# wineboot laten lopen, daarna pas de link zetten. De wineserver blijft draaien tot --stop (jvs_unlink_prefix stopt hem).
jvs_prewarm_prefix() {
  local pfx="$1" wb="${RG_WINE_BIN:-$HOME/.local/share/ravengun/wine/proton-11.0-2/bin}"
  WINEPREFIX="$pfx" WINEDEBUG=-all "$wb/wineserver" -p 2>/dev/null || return 1
  WINEPREFIX="$pfx" WINEDEBUG=-all timeout 90 "$wb/wine" cmd /c exit >/dev/null 2>&1 || true   # laat wineboot/mountmgr klaar zijn
  sleep 1
}

# jvs-route: de pty van de daemon als COM-poort in de Wine-prefix. Wat er stond (Wine maakt zelf com1..com32 -> /dev/ttyS*) wordt
# bewaard en bij het opruimen teruggezet. $1 = prefix, $2 = poort (bv. COM4).
jvs_link_prefix() {
  local pfx="$1" port="${2,,}" dd orig=""
  dd="$pfx/dosdevices"
  [[ -d "$dd" ]] || { echo "Geen $dd (prefix bestaat nog niet?)" >&2; return 1; }
  [[ -L "$dd/$port" ]] && orig=$(readlink "$dd/$port")
  [[ "$orig" == "$JVS_LINK" ]] && orig=$(sed -n 's/^ORIG=//p' "$JVS_STATE" 2>/dev/null | head -1)
  { echo "PREFIX=$pfx"; echo "PORT=$port"; echo "ORIG=$orig"; echo "WINESERVER=${RG_WINE_BIN:-$HOME/.local/share/ravengun/wine/proton-11.0-2/bin}/wineserver"; } > "$JVS_STATE"
  ln -sfn "$JVS_LINK" "$dd/$port"
}
# Zet de koppeling terug zoals ze was (alleen als ze nog naar onze link wijst).
jvs_unlink_prefix() {
  [[ -f "$JVS_STATE" ]] || return 0
  local pfx port orig ws
  ws=$(sed -n 's/^WINESERVER=//p' "$JVS_STATE" | head -1)
  pfx=$(sed -n 's/^PREFIX=//p' "$JVS_STATE" | head -1); port=$(sed -n 's/^PORT=//p' "$JVS_STATE" | head -1); orig=$(sed -n 's/^ORIG=//p' "$JVS_STATE" | head -1)
  if [[ -n "$pfx" && -n "$port" && -L "$pfx/dosdevices/$port" && "$(readlink "$pfx/dosdevices/$port")" == "$JVS_LINK" ]]; then
    if [[ -n "$orig" ]]; then ln -sfn "$orig" "$pfx/dosdevices/$port"; else rm -f "$pfx/dosdevices/$port"; fi
  fi
  # De (persistente) wineserver van onze eigen testprefix stoppen; alleen die prefix, nooit een andere.
  [[ -n "$ws" && -x "$ws" && -n "$pfx" ]] && WINEPREFIX="$pfx" "$ws" -k 2>/dev/null || true
  rm -f "$JVS_STATE"
}

game_running() {
  local p c
  [[ -n "$GAME_COMMS" ]] || build_game_comms
  for p in $(unit_pids); do
    { read -r c < "/proc/$p/comm"; } 2>/dev/null || continue
    c="${c,,}"
    [[ "$GAME_COMMS" == *"|$c|"* ]] && return 0
  done
  return 1
}

# Nog iets van de test over (welk proces dan ook in de unit-cgroup)?
unit_alive() { [[ -n "$(unit_pids)" ]]; }

stop_all() {
  # Oude route voor Halo: een BudgieLoader buiten de unit (bv. met de hand gestart) via de wineserver van de
  # plain-wine-prefix, die alleen Halo gebruikt.
  if pgrep -x "BudgieLoader_x6|BudgieLoader.ex" >/dev/null; then WINEPREFIX="$PREFIX" "$WINE/wineserver" -k 2>/dev/null || true; fi
  # De unit stoppen beeindigt TeknoParrotUi en alles wat het startte (wine, wineserver, game), met SIGKILL na 10 s.
  systemctl --user stop "$UNIT.service" 2>/dev/null || true
  for _ in $(seq 1 15); do unit_alive || break; sleep 1; done
  "$CTL" record --stop >/dev/null 2>&1 || true
  "$CTL" iobridge stop >/dev/null 2>&1 || true  # IO-brug (tp-shm, jvs): invoer neutraal, bestand/pty en route weg
  jvs_unlink_prefix                              # jvs: de COM-poort in de Wine-prefix terugzetten
  "$CTL" aagames stop >/dev/null 2>&1 || true   # Unity-route (play-unity.sh): de rumble gaat weer terug naar de trekker
  "$CTL" disarm >/dev/null 2>&1 || true
  if unit_alive; then echo "Er draaien nog processen van de test: $(unit_pids | tr '\n' ' ')" >&2; exit 1; fi
  echo "Game gestopt, uitvoer ontwapend."
}

RECORD=""; WAIT_MOVE=1; PROFILE=Halo; EXTRA_ENV=(); USE_TP=0; ROUTE=""; FORCE_NATIVE=0; LAUNCH_FORCED=""; LOG_FULL=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --stop) stop_all; exit 0 ;;
    --profile) PROFILE="${2:?--profile vereist een naam}"; shift 2 ;;
    --env) [[ "${2:-}" == *=* ]] || { echo "--env vereist K=V" >&2; exit 2; }; EXTRA_ENV+=("$2"); shift 2 ;;
    --gamescope) EXTRA_ENV+=(TP_GAMESCOPE=1 TP_GAMESCOPE_PATH=/usr/bin/gamescope); shift ;;
    --record) RECORD="${2:?--record vereist een tag}"; shift 2 ;;
    --no-wait-move) WAIT_MOVE=0; shift ;;
    --teknoparrot) USE_TP=1; shift ;;
    --native) FORCE_NATIVE=1; shift ;;
    --route) ROUTE="${2:?--route vereist evdev, tp-shm, unity-tcp, hid-board of jvs}"; shift 2 ;;
    --direct) LAUNCH_FORCED=direct; shift ;;
    --tpui) LAUNCH_FORCED=teknoparrot-ui; shift ;;
    --log-full) LOG_FULL=1; shift ;;
    *) echo "Gebruik: $0 [--profile NAAM] [--record TAG] [--env K=V]... [--gamescope] [--no-wait-move] [--teknoparrot] [--native] [--route R] [--direct|--tpui] [--log-full] | --stop" >&2; exit 2 ;;
  esac
done

# Invoerroute: --route, anders de keuze van de gebruiker (~/.config/ravengun/io-routes.json, gezet door de app) over
# packaging/io-routes.json (standaard evdev); zie scripts/io-route.py. --teknoparrot forceert evdev.
ROUTE_FORCED="$ROUTE"
if [[ -z "$ROUTE" ]]; then
  ROUTE=$(python3 -I "$ROOT/scripts/io-route.py" --root "$ROOT" "$PROFILE" route 2>/dev/null || echo evdev)
  [[ -n "$ROUTE" ]] || ROUTE=evdev
fi
[[ $USE_TP -eq 1 ]] && ROUTE=evdev

# Native route (zonder TeknoParrot/Wine): profiel in packaging/native-games.json. enabled=true is de standaard, --native forceert.
NATIVE=0; NATIVE_EXE=""
if [[ $USE_TP -eq 0 ]]; then
  NINFO=$(python3 -I - "$ROOT/packaging/native-games.json" "$PROFILE" <<'PY' 2>/dev/null || true
import json, sys
try:
    g = json.load(open(sys.argv[1]))["games"].get(sys.argv[2])
    if g:
        print("%d\t%s\t%s" % (1 if g.get("enabled") else 0, g.get("exe", "").rsplit("/", 1)[-1], g.get("route", "")))
except Exception:
    pass
PY
)
  if [[ -n "$NINFO" ]]; then
    IFS=$'\t' read -r NEN NEXE NROUTE <<<"$NINFO"
    if [[ "$NEN" == 1 || $FORCE_NATIVE -eq 1 ]]; then NATIVE=1; NATIVE_EXE="$NEXE"; fi
    # De invoerroute van een native game staat in native-games.json (tenzij --route die overschrijft).
    [[ $NATIVE -eq 1 && -z "$ROUTE_FORCED" && -n "$NROUTE" ]] && ROUTE="$NROUTE"
  elif [[ $FORCE_NATIVE -eq 1 ]]; then
    fail "Profiel $PROFILE staat niet in packaging/native-games.json (zie scripts/play-native.sh --list)."
  fi
fi
case "$ROUTE" in evdev|tp-shm|unity-tcp|hid-board|jvs) ;; *) fail "Onbekende route: $ROUTE (evdev, tp-shm, unity-tcp, hid-board of jvs)";; esac

# Unity-route (zonder TeknoParrot): het profiel staat in packaging/aagames-games.json.
if [[ $USE_TP -eq 0 && $NATIVE -eq 0 && "$ROUTE" != tp-shm ]] && grep -qE "^[[:space:]]{4}\"$PROFILE\":[[:space:]]*\{" "$ROOT/packaging/aagames-games.json" 2>/dev/null; then
  UARGS=(--game "$PROFILE")
  [[ $WAIT_MOVE -eq 0 ]] && UARGS+=(--no-wait-move)
  for E in "${EXTRA_ENV[@]}"; do UARGS+=(--env "$E"); done
  # Geen exec: de webapp herkent een lopende start aan het proces van dit script (games.cpp, script_state).
  "$ROOT/scripts/play-unity.sh" "${UARGS[@]}"
  exit $?
fi
[[ $NATIVE -eq 1 || -f "$TP/UserProfiles/$PROFILE.xml" ]] || fail "Onbekend profiel: $PROFILE (zie scripts/tp-add-game.py)"

# Startwijze (A10): direct (scripts/launch-direct.py, zonder TeknoParrotUi) of teknoparrot-ui. Volgorde: --direct/--tpui, anders
# "launch" in io-routes.json (gebruikersbestand over de basis), anders teknoparrot-ui. --teknoparrot (route evdev) betekent de UI.
LAUNCH=teknoparrot-ui
if [[ $NATIVE -eq 0 ]]; then
  if [[ -n "$LAUNCH_FORCED" ]]; then LAUNCH="$LAUNCH_FORCED"
  elif [[ $USE_TP -eq 0 ]]; then LAUNCH=$(python3 -I "$ROOT/scripts/io-route.py" --root "$ROOT" "$PROFILE" launch 2>/dev/null || echo teknoparrot-ui); fi
  [[ $USE_TP -eq 1 && "$LAUNCH_FORCED" == direct ]] && fail "--teknoparrot (TeknoParrotUi met evdev) en --direct gaan niet samen."
fi
[[ "$LAUNCH" == direct || "$LAUNCH" == teknoparrot-ui ]] || LAUNCH=teknoparrot-ui
# Direct kan niet terugvallen op het lezen van evdev door TeknoParrotUi: de daemon schrijft dan de invoer (tp-shm).
direct_fallback() {   # $1 = reden; met --direct is dat een fout, anders via TeknoParrotUi
  [[ "$LAUNCH_FORCED" == direct ]] && fail "Directe start van $PROFILE niet mogelijk: $1"
  say "Directe start niet mogelijk ($1); start via TeknoParrotUi."
  LAUNCH=teknoparrot-ui
}
if [[ "$LAUNCH" == direct ]]; then
  if [[ "$ROUTE" == evdev ]]; then
    if [[ -n "$(python3 -I "$ROOT/scripts/io-route.py" --root "$ROOT" "$PROFILE" layout 2>/dev/null)" ]]; then
      ROUTE=tp-shm; echo "Directe start: invoer via tp-shm (zonder TeknoParrotUi leest niemand evdev)."
    else
      direct_fallback "route evdev zonder tp-shm-indeling"
    fi
  fi
fi
if [[ "$LAUNCH" == direct ]]; then
  RC=0; CHK=$(python3 -I "$ROOT/scripts/launch-direct.py" "$PROFILE" --check 2>&1) || RC=$?
  [[ $RC -eq 0 ]] || direct_fallback "${CHK##*$'\n'}"
fi
# Eén game tegelijk: game_running kent alleen de processen van het profiel dat nu start; elke levende
# unit-cgroup betekent dat er al iets draait (anders zou de stop hieronder die game stil afbreken).
if game_running || unit_alive; then
  fail "There is already a game running; stop it first (Esc, or 'Stop' in RavenGun). / Er draait al een game; stop die eerst."
fi
systemctl --user is-active --quiet ravengund.service || systemctl --user start ravengund.service
for _ in $(seq 1 20); do "$CTL" status >/dev/null 2>&1 && break; sleep 0.5; done

if [[ $WAIT_MOVE -eq 1 ]]; then
  say "Zet de Move aan: druk op de PS-knop."
  for _ in $(seq 1 120); do "$CTL" status 2>/dev/null | grep -q '^Move:.*verbonden (' && break; sleep 1; done
  "$CTL" status 2>/dev/null | grep -q '^Move:.*verbonden (' || fail "Geen Move gevonden binnen 2 minuten. Druk op PS en start opnieuw."
fi

"$CTL" arm >/dev/null
for L in usb-RavenGun_Virtual_Lightgun-event-mouse usb-RavenGun_Virtual_Keyboard-event-kbd; do
  for _ in $(seq 1 20); do [[ -e /dev/input/by-id/$L ]] && break; sleep 0.25; done
  [[ -e /dev/input/by-id/$L ]] || fail "Invoerapparaat ontbreekt: $L"
done
# tp-shm: de daemon neemt de invoer van de TeknoParrot-kern over (bestand maken voor de game start).
SWAP=""
if [[ "$ROUTE" == tp-shm ]]; then
  "$CTL" iobridge start "$PROFILE" --route tp-shm >/dev/null || fail "IO-brug (tp-shm) starten mislukt voor $PROFILE"
  # De swap hoort bij de Wine-hulp van TeknoParrot; een native game heeft die niet.
  # Direct: launch-direct.py start pipehelper.exe zelf met het bestand van de daemon als bron; geen swap nodig.
  [[ $NATIVE -eq 1 || "$LAUNCH" == direct ]] || SWAP="'$ROOT/scripts/tp-shm-swap.sh' >>'$LOGDIR/tpshm-$(date +%Y%m%d-%H%M%S).log' 2>&1 &"
  say "Invoerroute: tp-shm (de daemon schrijft de invoer)."
elif [[ "$ROUTE" == hid-board ]]; then
  # Het bord moet bestaan voordat de game start (DirectInput telt de joysticks bij het openen). Eerst de prefix: het bord
  # krijgt daar joystick-id 0, want GUID_Joystick is de eerste joystick (anders de Move zelf).
  WPFX=$(sed -n 's/^RG_WINEPREFIX=//p' "$TP/UserProfiles/$PROFILE.env" 2>/dev/null | head -1)
  VIDPID=$(python3 -I "$ROOT/scripts/io-route.py" --root "$ROOT" "$PROFILE" hid_vidpid 2>/dev/null || true)
  "$CTL" iobridge start "$PROFILE" --route hid-board >/dev/null || fail "IO-brug (hid-board) starten mislukt voor $PROFILE"
  if [[ -n "$WPFX" && -n "$VIDPID" ]]; then "$ROOT/scripts/wine-joyid.sh" "$WPFX" "$VIDPID" || true; fi
  say "Invoerroute: hid-board (de daemon is het invoerbord van de game)."
elif [[ "$ROUTE" == jvs ]]; then
  # De daemon maakt de pty (en de symlink $JVS_LINK); die komt in de Wine-prefix van het profiel als COM-poort. De LD_PRELOAD-hulp
  # laat GetCommModemStatus op die pty slagen (een pty kent TIOCMGET niet). Eerst het env-bestand lezen: de prefix staat daar.
  WPFX=$(sed -n 's/^RG_WINEPREFIX=//p' "$TP/UserProfiles/$PROFILE.env" 2>/dev/null | head -1)
  [[ -n "$WPFX" ]] || fail "Route jvs vraagt RG_WINEPREFIX=<eigen prefix> in $TP/UserProfiles/$PROFILE.env (en WineRunnerPath = wrappers/wine-prefix)."
  COMPORT=$(python3 -I "$ROOT/scripts/io-route.py" --root "$ROOT" "$PROFILE" com_port 2>/dev/null || echo COM4)
  [[ -n "$COMPORT" ]] || COMPORT=COM4
  say "Wine-prefix voorbereiden (wineboot), daarna de poort koppelen..."
  jvs_prewarm_prefix "$WPFX" || fail "Kon de wineserver van $WPFX niet starten."
  "$CTL" iobridge start "$PROFILE" --route jvs >/dev/null || { jvs_unlink_prefix; fail "IO-brug (jvs) starten mislukt voor $PROFILE"; }
  [[ -L "$JVS_LINK" ]] || { "$CTL" iobridge stop >/dev/null 2>&1 || true; fail "De daemon maakte geen pty-symlink ($JVS_LINK)."; }
  jvs_link_prefix "$WPFX" "$COMPORT" || { "$CTL" iobridge stop >/dev/null 2>&1 || true; fail "Kon $COMPORT in $WPFX niet koppelen."; }
  if [[ -r "$RG_LIB/lib32/librg_comm_shim.so" && -r "$RG_LIB/librg_comm_shim.so" ]]; then EXTRA_ENV+=("RG_LD_PRELOAD=$SHIM"); else echo "Let op: de LD_PRELOAD-hulp ontbreekt (cmake --build build); GetCommModemStatus faalt dan op de pty." >&2; fi
  say "Invoerroute: jvs (de daemon is het JVS-bord op $COMPORT)."
else
  "$CTL" iobridge stop >/dev/null 2>&1 || true   # een rest van een eerdere tp-shm-sessie
fi
[[ -n "$RECORD" ]] && "$CTL" record --start --tag "$RECORD" --duration 3600 >/dev/null && echo "Opname gestart ($RECORD)."

# Schermomgeving van de ingelogde sessie (ook bij starten via SSH).
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}" DISPLAY="${DISPLAY:-:0}"
export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=$XDG_RUNTIME_DIR/bus}"
if [[ -z "${XAUTHORITY:-}" ]]; then
  KP=$(pgrep -x kwin_wayland | head -1 || true)
  if [[ -n "$KP" ]]; then
    mapfile -d '' ARGS < "/proc/$KP/cmdline"
    for i in "${!ARGS[@]}"; do [[ ${ARGS[$i]} == --xwayland-xauthority ]] && export XAUTHORITY="${ARGS[$((i+1))]}"; done
  fi
fi

# Per-profiel omgevingsvariabelen: UserProfiles/<PROFIEL>.env, daarna --env (laatste wint).
ENVFILE="$TP/UserProfiles/$PROFILE.env"
if [[ -f "$ENVFILE" ]]; then
  while IFS= read -r L || [[ -n "$L" ]]; do
    L="${L%%#*}"; L="${L#"${L%%[![:space:]]*}"}"; L="${L%"${L##*[![:space:]]}"}"
    [[ "$L" == *=* ]] && EXTRA_ENV=("$L" "${EXTRA_ENV[@]}")
  done < "$ENVFILE"
fi
SDENV=(); for E in "${EXTRA_ENV[@]}"; do SDENV+=(-E "$E"); done
[[ ${#EXTRA_ENV[@]} -gt 0 ]] && echo "Extra omgeving: ${EXTRA_ENV[*]}"

mkdir -p "$LOGDIR"; LOG="$LOGDIR/tpui-$(date +%Y%m%d-%H%M%S).log"
systemctl --user stop "$UNIT.service" 2>/dev/null || true
if [[ $NATIVE -eq 1 ]]; then
  # Native route: play-native.sh in dezelfde user-unit (ravengun-tpui), zodat stoppen en de OLED-beschermer werken als bij TeknoParrot.
  # De extra omgevingsvariabelen (--env) gaan als --env naar de game zelf.
  NARGS=(--game "$PROFILE"); for E in "${EXTRA_ENV[@]}"; do NARGS+=(--env "$E"); done
  systemd-run --user --unit="$UNIT" --collect --quiet --working-directory="$ROOT" -p TimeoutStopSec=10 \
    -E XDG_RUNTIME_DIR -E WAYLAND_DISPLAY -E DISPLAY -E DBUS_SESSION_BUS_ADDRESS ${XAUTHORITY:+-E XAUTHORITY} \
    /bin/sh -c "$SWAP exec '$ROOT/scripts/play-native.sh' ${NARGS[*]@Q} >>'$LOG' 2>&1"
elif [[ "$LAUNCH" == direct ]]; then
  # Directe start (A10): launch-direct.py is het hoofdproces van dezelfde unit, op de plaats van TeknoParrotUi. Het leest zelf
  # UserProfiles/<P>.env; de extra omgeving (--env) gaat ook mee als omgeving van de unit, net als bij TeknoParrotUi.
  LOG="$LOGDIR/direct-$(date +%Y%m%d-%H%M%S).log"
  DARGS=(--run); [[ "$ROUTE" == tp-shm ]] && DARGS+=(--shm-source /dev/shm/ravengun_JvsState); [[ $LOG_FULL -eq 1 ]] && DARGS+=(--log-full)
  say "Start direct (zonder TeknoParrotUi)."
  systemd-run --user --unit="$UNIT" --collect --quiet --working-directory="$TP" -p TimeoutStopSec=10 \
    -E XDG_RUNTIME_DIR -E WAYLAND_DISPLAY -E DISPLAY -E DBUS_SESSION_BUS_ADDRESS ${XAUTHORITY:+-E XAUTHORITY} "${SDENV[@]}" \
    /bin/sh -c "exec python3 -I '$ROOT/scripts/launch-direct.py' '$PROFILE' ${DARGS[*]} >>'$LOG' 2>&1"
else
systemd-run --user --unit="$UNIT" --collect --quiet --working-directory="$TP" -p TimeoutStopSec=10 \
  -E XDG_RUNTIME_DIR -E WAYLAND_DISPLAY -E DISPLAY -E DBUS_SESSION_BUS_ADDRESS ${XAUTHORITY:+-E XAUTHORITY} "${SDENV[@]}" \
  /bin/sh -c "$SWAP exec ./TeknoParrotUi --profile=$PROFILE.xml >>'$LOG' 2>&1"
fi

for _ in $(seq 1 "${RAVENGUN_START_WAIT:-120}"); do game_running && break; sleep 1; done
game_running || { [[ "$ROUTE" == jvs ]] && { "$CTL" iobridge stop >/dev/null 2>&1 || true; jvs_unlink_prefix; }; fail "$PROFILE is niet gestart; zie $LOG"; }
say "$PROFILE start. Move verbonden, lightgun klaar."
