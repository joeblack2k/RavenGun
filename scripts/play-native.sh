#!/usr/bin/env bash
# RavenGun: start een Raw Thrills-game native op Linux (zonder Wine en zonder BudgieLoader) in een bubblewrap-sandbox.
#
# Per game staat in packaging/native-games.json: de NAS-map(pen) en waar de game ze verwacht (bv. /pm/g7/halo), de vooraf te
# laden bibliotheek van TeknoParrot (alleen halo.so voor Halo; er is geen eigen dongle-, keychip- of integriteitsafhandeling),
# losse bibliotheken, startparameters en resolutie. De NAS blijft alleen-lezen: alles wat de game schrijft (pmuser/, audits, logs)
# komt in een lokale overlay onder ~/.local/share/ravengun/native/<PROFIEL>/. Er wordt niets van de game gekopieerd.
#
# Gebruik: scripts/play-native.sh --game PROFIEL [opties] [-- extra game-argumenten]
#          scripts/play-native.sh --list | --stage-libs | --stop
#   --game P       profiel uit packaging/native-games.json (Halo, JurassicPark, Terminator, GhostBusters, ...)
#   --res WxH      resolutie die de game krijgt (standaard: uit het gegevensbestand, daarna de schermgrootte)
#   --headless     draai in gamescope zonder beeldscherm (tv blijft vrij; voor tests, geen geluid)
#   --gamescope    draai in gamescope beeldvullend op de tv (schaalt een kleinere resolutie op)
#   --seconds N    stop de game na N seconden (tests)
#   --snap F       alleen met --headless en --seconds: sla vlak voor het einde een schermafbeelding (png) op
#   --strace F     draai de game onder strace (alleen openat/open/ioctl/connect/execve naar bestand F)
#   --gdb          draai de game onder gdb (batch; geeft bij een crash een backtrace in het logboek)
#   --input-nodes "eventN jsN"  toon alleen deze invoerapparaten in /dev/input (standaard alles; handig om de Move als SDL-joystick te verbergen)
#   --mute         geen geluidsverbinding in de sandbox
#   --bind-ro S D  toon bestand of map S read-only als D in de sandbox (herhaalbaar)
#   --bind-rw S D  toon bestand S schrijfbaar als D (voor apparaten met open(O_RDWR)+mmap; herhaalbaar)
#   --share-ipc    host-IPC niet isoleren (nodig voor X MIT-SHM; bewuste sandbox-afweging)
#   --loader       de game via onze eigen rg-elfloader starten i.p.v. de kernel-ld.so (profiel: "loader": "rg-elfloader")
#   --env K=V      extra omgevingsvariabele voor de game (herhaalbaar)
#   --debug        LD_DEBUG=libs en uitgebreid logboek
#   --dry-run      toon alleen het bwrap-commando
#   --stage-libs   zet de i386-bibliotheken klaar die het gegevensbestand met @lib32 noemt (uit de Steam Linux Runtime)
#   --stop         stop de lopende native game (alleen onze eigen launcher)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/scripts/rg-paths.sh"   # RG_BIN en RG_LIB: repo (build/) of installatie (<prefix>/bin en lib)
. "$ROOT/scripts/rg-identiteit.sh"   # rg_identiteit: welk script dit is (pad, sha256, repo of installatie), voor het logboek
JSON="$ROOT/packaging/native-games.json"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}/ravengun"
STATE="${XDG_STATE_HOME:-$HOME/.local/state}/ravengun"
NAS="${RAVENGUN_NAS_TP:-$HOME/mnt/spidernas_emulation/Teknoparrot}"
TPUI="${RAVENGUN_TPUI:-$DATA/runtime/TeknoParrotUi-2.0.0.130-linux-x64}"
LIB32="$DATA/runtime/lib32"
RUNDIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
IPC="$RUNDIR/ravengun"
LOGDIR="$STATE/logs"

die() { echo "play-native: $*" >&2; exit 1; }

BINDS=(); RW=(); SHARE_IPC=0; USE_LOADER=0; GAME=""; RES=""; HEADLESS=0; GAMESCOPE=0; SECONDS_MAX=0; SNAP=""; STRACE=""; INPUT_NODES=""; GDB=0; MUTE=0; DEBUG=0; DRY=0; EXTRA=(); XENV=()
ACTION=run
while [[ $# -gt 0 ]]; do
  case "$1" in
    --game) GAME="${2:?--game vereist een profiel}"; shift ;;
    --res) RES="${2:?}"; shift ;;
    --headless) HEADLESS=1 ;;
    --gamescope) GAMESCOPE=1 ;;
    --seconds) SECONDS_MAX="${2:?}"; shift ;;
    --snap) SNAP="${2:?}"; shift ;;
    --strace) STRACE="${2:?}"; shift ;;
    --gdb) GDB=1 ;;
    --input-nodes) INPUT_NODES="${2:?}"; shift ;;
    --mute) MUTE=1 ;;
    --bind-ro) [[ -n "${2:-}" && -n "${3:-}" ]] || die "--bind-ro vereist bron en doel"; BINDS+=("$2" "$3"); shift 2 ;;
    --bind-rw) [[ -n "${2:-}" && -n "${3:-}" ]] || die "--bind-rw vereist bron en doel"; RW+=("$2" "$3"); shift 2 ;;
    --share-ipc) SHARE_IPC=1 ;;   # host-IPC niet isoleren (nodig voor X MIT-SHM; bewuste sandbox-afweging)
    --loader) USE_LOADER=1 ;;     # de game via onze eigen rg-elfloader starten i.p.v. de kernel-ld.so
    --env) [[ "${2:-}" == *=* ]] || die "--env vereist K=V"; XENV+=("$2"); shift ;;
    --debug) DEBUG=1 ;;
    --dry-run) DRY=1 ;;
    --list) ACTION=list ;;
    --stage-libs) ACTION=stage ;;
    --stop) ACTION=stop ;;
    --) shift; EXTRA=("$@"); break ;;
    -h|--help) sed -n '2,26p' "$0"; exit 0 ;;
    *) die "onbekende optie: $1" ;;
  esac
  shift
done

# Het gegevensbestand als regels voor de shell: soort<TAB>veld1<TAB>veld2<TAB>veld3. Paden met @nas, @tpui, @data en @lib32 worden hier ingevuld.
emit() {
  python3 -I - "$JSON" "$1" "$NAS" "$TPUI" "$DATA" "$LIB32" "$RG_LIB" <<'PY'
import json, sys
path, game, nas, tpui, data, lib32, build = sys.argv[1:8]
d = json.load(open(path))
def ex(s):
    for k, v in (("@nas", nas), ("@tpui", tpui), ("@data", data), ("@lib32", lib32), ("@build", build)):
        s = s.replace(k, v)
    return s
if game == "--list":
    for n, g in d["games"].items():
        print("\t".join(["GAME", n, g.get("status", "?"), "aan" if g.get("enabled") else "uit", g.get("title", n)]))
    sys.exit(0)
g = d["games"].get(game)
if g is None:
    print("ERR\tonbekend profiel: %s" % game); sys.exit(0)
dflt = d.get("defaults", {})
for k in ("title", "arch", "preload", "exe", "cwd", "home", "status", "note", "route", "layout", "launcher", "loader"):
    print("VAR\t%s\t%s" % (k, g.get(k, "")))
print("VAR\tenabled\t%s" % ("1" if g.get("enabled") else "0"))
print("VAR\tshare_ipc\t%s" % ("1" if g.get("share_ipc") else "0"))
print("VAR\tres\t%s" % g.get("res", dflt.get("res", "1920x1080")))
for b in g.get("binds_rw", []):
    print("RWBIND\t%s\t%s" % (ex(b[0]), ex(b[1])))
for m in g.get("mounts", []):
    print("MOUNT\t%s\t%s\t%s" % (m.get("mode", "ro"), ex(m["src"]), m["dst"]))
for f in g.get("files", []):
    print("FILE\t%s\t%s" % (ex(f["src"]), f["dst"]))
for l in g.get("libs", []):
    print("LIB\t%s" % ex(l))
for e in g.get("extra_ro", []):
    print("EXTRA\t%s" % ex(e))
for l in g.get("libdirs", []):
    print("LIBDIR\t%s\t%s" % (ex(l["src"]), l["dst"]))
for a in g.get("args", []):
    print("ARG\t%s" % a)
for k, v in g.get("env", {}).items():
    print("ENV\t%s\t%s" % (k, v))
PY
}

if [[ "$ACTION" == list ]]; then
  printf '%-16s %-12s %-5s %s\n' PROFIEL STATUS AUTO TITEL
  while IFS=$'\t' read -r kind a b c d; do [[ "$kind" == GAME ]] && printf '%-16s %-12s %-5s %s\n' "$a" "$b" "$c" "$d"; done < <(emit --list)
  exit 0
fi

if [[ "$ACTION" == stage ]]; then
  # i386-bibliotheken die niet in /usr/lib32 of in de ElfLdr2-map staan, uit de Steam Linux Runtime (alleen bibliotheken, geen game).
  mkdir -p "$LIB32"
  SN=""
  for c in /run/media/deck/steam/SteamLibrary/steamapps/common/SteamLinuxRuntime_sniper/var/tmp-*/usr/lib/i386-linux-gnu \
           "$HOME"/.local/share/Steam/steamapps/common/SteamLinuxRuntime_sniper/var/tmp-*/usr/lib/i386-linux-gnu; do
    [[ -e "$c/libglut.so.3" ]] && SN="$c" && break
  done
  [[ -n "$SN" ]] || die "geen i386-runtime met libglut gevonden (Steam Linux Runtime sniper)"
  for l in libglut.so.3 libXi.so.6 libGLU.so.1 libtheora.so.0 libXrandr.so.2 libXrender.so.1; do
    [[ -e "$SN/$l" ]] || { echo "ontbreekt in de runtime: $l" >&2; continue; }
    cp -L --no-preserve=ownership "$SN/$l" "$LIB32/$l"; echo "gezet: $LIB32/$l"
  done
  # libSDL-1.2 (voor Jurassic Park) is er niet voor i386: sdl12-compat uit de multilib-opslagplaats van SteamOS, uitgepakt in een eigen tijdelijke map.
  if [[ ! -e "$LIB32/libSDL-1.2.so.0" ]]; then
    for l in libSDL2-2.0.so.0; do [[ -e "$SN/$l" ]] && cp -L "$SN/$l" "$LIB32/$l" && echo "gezet: $LIB32/$l"; done
    T=$(mktemp -d); curl -fsS --max-time 60 -o "$T/p.tar.zst" "https://steamdeck-packages.steamos.cloud/archlinux-mirror/multilib-3.9/os/x86_64/lib32-sdl12-compat-1.2.68-2-x86_64.pkg.tar.zst" \
      && mkdir "$T/x" && tar -C "$T/x" -xf "$T/p.tar.zst" && cp -L "$T/x/usr/lib32/libSDL-1.2.so.1.2.68" "$LIB32/libSDL-1.2.so.0" && echo "gezet: $LIB32/libSDL-1.2.so.0" \
      || echo "libSDL-1.2 ophalen mislukt" >&2
    rm -rf "$T"
  fi
  exit 0
fi

if [[ "$ACTION" == stop ]]; then
  PIDF="$IPC/native.pid"
  [[ -f "$PIDF" ]] || { echo "play-native: geen actieve native launcher"; exit 0; }
  PID=$(cat "$PIDF")
  grep -q 'play-native.sh' "/proc/$PID/cmdline" 2>/dev/null || { echo "play-native: pidfile verouderd; verwijderd"; rm -f "$PIDF"; exit 0; }
  kill -TERM "$PID"
  for _ in $(seq 1 20); do kill -0 "$PID" 2>/dev/null || { echo "play-native: gestopt"; exit 0; }; sleep 0.5; done
  die "launcher $PID reageert niet op TERM binnen 10 s"
fi

[[ -n "$GAME" ]] || die "--game vereist (zie --list)"
declare -A V=(); EXTRAS=(); MOUNTS=(); FILES=(); LIBS=(); LIBDIRS=(); GARGS=(); GENV=()
while IFS=$'\t' read -r kind a b c; do
  case "$kind" in
    ERR) die "$a" ;;
    VAR) V["$a"]="$b" ;;
    MOUNT) MOUNTS+=("$a"$'\t'"$b"$'\t'"$c") ;;
    FILE) FILES+=("$a"$'\t'"$b") ;;
    LIB) LIBS+=("$a") ;;
    LIBDIR) LIBDIRS+=("$a"$'\t'"$b") ;;
    EXTRA) EXTRAS+=("$a") ;;
    ARG) GARGS+=("$a") ;;
    ENV) GENV+=("$a=$b") ;;
    RWBIND) RW+=("$a" "$b") ;;
  esac
done < <(emit "$GAME")
# Profiel kan ook om host-IPC vragen (X MIT-SHM); de CLI-vlag --share-ipc blijft ook werken.
[[ "${V[share_ipc]:-0}" == 1 ]] && SHARE_IPC=1
# Profiel kan onze eigen lader vragen ("loader": "rg-elfloader"); CLI-vlag --loader blijft ook werken.
[[ -n "${V[loader]:-}" ]] && USE_LOADER=1

# De invoertabel van deze game (docs/per-game-patches.md, "rt-invoer.ini"): WAT de invoerhulp (librg_rt_input.so) waarheen
# schrijft -- schakelaars, geweerknoppen, assen -- staat per game in patches/<Game>/rt-invoer.ini, niet in de hulp. De
# gebruikerslaag ($DATA/patches/<Game>/rt-invoer.ini) gaat als heel bestand voor de meegeleverde. De tabel gaat alleen-lezen de
# sandbox in en de hulp krijgt het pad. Staat er geen tabel, of gebruikt de run de hulp niet, dan verandert er niets: de hulp doet
# dan wat hij altijd deed. Een RG_RT_TABLE die met --env is meegegeven gaat voor.
RT_TABEL_DOEL=/rg/tabel/rt-invoer.ini
rt_hulp=0; rt_eigen=0
if [[ "${V[preload]}" == *librg_rt_input.so* ]]; then rt_hulp=1; fi
for e in "${XENV[@]}"; do
  if [[ "$e" == LD_PRELOAD=*librg_rt_input.so* ]]; then rt_hulp=1; fi
  if [[ "$e" == RG_RT_TABLE=* ]]; then rt_eigen=1; fi
done
if (( rt_hulp == 1 && rt_eigen == 0 )); then
  for tabel in "$DATA/patches/$GAME/rt-invoer.ini" "$ROOT/patches/$GAME/rt-invoer.ini"; do
    if [[ -f "$tabel" ]]; then BINDS+=("$tabel" "$RT_TABEL_DOEL"); XENV+=("RG_RT_TABLE=$RT_TABEL_DOEL"); break; fi
  done
fi

command -v bwrap >/dev/null || die "bwrap niet gevonden"
[[ -d "$NAS" ]] || die "NAS-map $NAS niet bereikbaar (is de automount actief?)"
OVLBASE="$DATA/native/$GAME"; mkdir -p "$OVLBASE" "$LOGDIR" "$IPC"; chmod 700 "$IPC"

# --- sandbox opbouwen --------------------------------------------------------
BW=(bwrap --die-with-parent --unshare-pid --unshare-uts --unshare-net
  --ro-bind /usr /usr --symlink usr/lib /lib --symlink usr/lib64 /lib64 --symlink usr/bin /bin --symlink usr/bin /sbin
  --ro-bind /etc /etc --proc /proc --dev /dev
  --dev-bind /dev/dri /dev/dri --dev-bind-try /dev/snd /dev/snd
  --ro-bind /sys /sys --ro-bind-try /run/udev /run/udev
  --tmpfs /tmp)
# Standaard isoleren we ook de IPC-namespace. Maar X MIT-SHM (XShmAttach) deelt SysV-shm met de
# X-server; in een eigen IPC-namespace faalt dat met BadAccess en crasht de game (gemeten: GoGoStrike
# in set_gfx_mode). --share-ipc laat de host-IPC toe. Bewuste afweging: minder sandbox-isolatie voor
# die game, in ruil voor werkende gedeelde-geheugen-grafiek. Zie docs/research/gogostrike-hobbel2.md.
(( SHARE_IPC )) || BW+=(--unshare-ipc)

# Invoerapparaten: alles, of alleen de genoemde nodes.
if [[ -n "$INPUT_NODES" ]]; then
  BW+=(--dir /dev/input); for n in $INPUT_NODES; do BW+=(--dev-bind "/dev/input/$n" "/dev/input/$n"); done
else
  BW+=(--dev-bind-try /dev/input /dev/input)
fi

# De camera (docs/per-game-patches.md, "De camera"): alleen een spel dat de camera IN het spel gebruikt ([Camera] Use = game in
# game.ini) krijgt hem, en dan de camera die in de instellingen gekozen is; een spel dat hem alleen voor de inrichting wil, of er
# niets mee doet, krijgt er geen. Met --dev-bind, zoals /dev/dri: de toegang van de gebruiker komt van een ACL op het apparaat en
# die overleeft een gewone --bind niet (gemeten: open() geeft dan Permission denied). -try: een machine zonder die camera geeft
# geen fout. Zonder [Camera] verandert er niets aan het commando; het logboek zegt bij elke run welke keuze er gemaakt is.
CAM=$(python3 -I "$ROOT/scripts/camera.py" resolve "$GAME") || die "de camera-instelling van $GAME klopt niet (zie de melding hierboven)"
IFS=$'\t' read -r CAMERA cam_rol cam_uitleg <<<"$CAM"
if [[ "$CAMERA" == - || -z "$CAMERA" ]]; then
  CAMERA=""; CAMERA_REGEL="camera: geen (rol ${cam_rol:-none}: ${cam_uitleg:-geen instelling})"
else
  BW+=(--dev-bind-try "$CAMERA" "$CAMERA")
  CAMERA_REGEL="camera: $CAMERA doorgegeven (rol $cam_rol: $cam_uitleg)"
fi

# De NAS-mounts. overlay: NAS als lezende onderlaag, schrijfacties naar $OVLBASE/<n>/{upper,work}.
n=0
for m in "${MOUNTS[@]}"; do
  IFS=$'\t' read -r mode src dst <<<"$m"
  [[ -d "$src" ]] || die "bron ontbreekt: $src"
  case "$mode" in
    overlay) mkdir -p "$OVLBASE/$n/upper" "$OVLBASE/$n/work"; BW+=(--overlay-src "$src" --overlay "$OVLBASE/$n/upper" "$OVLBASE/$n/work" "$dst") ;;
    ro) BW+=(--ro-bind "$src" "$dst") ;;
    *) die "onbekende mount-modus: $mode" ;;
  esac
  n=$((n + 1))
done
for f in "${FILES[@]}"; do
  IFS=$'\t' read -r src dst <<<"$f"
  [[ -e "$src" ]] || die "bestand ontbreekt: $src"
  BW+=(--ro-bind "$src" "$dst")
done
LDPATH=""
for l in "${LIBDIRS[@]}"; do
  IFS=$'\t' read -r src dst <<<"$l"
  [[ -d "$src" ]] || die "bibliotheekmap ontbreekt: $src"
  BW+=(--ro-bind "$src" "$dst"); LDPATH+="${LDPATH:+:}$dst"
done
if (( ${#LIBS[@]} )); then
  MISSING=()
  for l in "${LIBS[@]}"; do
    [[ -e "$l" ]] || { MISSING+=("$l"); continue; }
    BW+=(--ro-bind "$l" "/rg/lib/$(basename "$l")")
  done
  (( ${#MISSING[@]} == 0 )) || die "bibliotheken ontbreken: ${MISSING[*]} (probeer --stage-libs)"
  LDPATH+="${LDPATH:+:}/rg/lib"
fi

for x in "${EXTRAS[@]}"; do BW+=(--ro-bind-try "$x" "$x"); done
for ((i = 0; i < ${#BINDS[@]}; i += 2)); do BW+=(--ro-bind "${BINDS[$i]}" "${BINDS[$((i+1))]}"); done
# rw-bestandsbind: sommige apparaten (GoGoStrike: /dev/w040101sram0, de PAOKAI-nodes) worden met
# open(O_RDWR)+mmap(MAP_SHARED) geopend; --ro-bind volstaat niet. bwrap maakt het doelpad aan.
for ((i = 0; i < ${#RW[@]}; i += 2)); do BW+=(--bind "${RW[$i]}" "${RW[$((i+1))]}"); done

# --- scherm en geluid ---------------------------------------------------------
if (( HEADLESS || GAMESCOPE )) && [[ -z "${RG_NATIVE_INNER:-}" ]]; then
  command -v gamescope >/dev/null || die "gamescope niet gevonden"
fi
if [[ -z "${DISPLAY:-}" || ( -z "${XAUTHORITY:-}" && -z "${RG_NATIVE_INNER:-}" ) ]]; then
  KWPID=$(pgrep -u "$(id -u)" -x kwin_wayland | head -1 || true)
  if [[ -n "$KWPID" ]]; then
    mapfile -d '' -t KWARGS < "/proc/$KWPID/cmdline"
    for (( i = 0; i < ${#KWARGS[@]} - 1; i++ )); do
      case "${KWARGS[$i]}" in
        --xwayland-display)    DISPLAY="${DISPLAY:-${KWARGS[$((i+1))]}}" ;;
        --xwayland-xauthority) XAUTHORITY="${XAUTHORITY:-${KWARGS[$((i+1))]}}" ;;
      esac
    done
  fi
fi

RES="${RES:-${V[res]:-1920x1080}}"
# launcher "gamescope" in het gegevensbestand: schaal beeldvullend op de tv, behalve in een gamescope die al om ons heen draait.
[[ "${V[launcher]}" == gamescope && $HEADLESS -eq 0 ]] && GAMESCOPE=1
GAMECMD=("${V[exe]}")
for a in "${GARGS[@]}"; do GAMECMD+=("${a//\{res\}/$RES}"); done
GAMECMD+=("${EXTRA[@]}")

# Onze eigen lader (rg-elfloader) voor de ElfLdr2-familie i.p.v. de kernel-ld.so. De lader-binary wordt in
# de sandbox gebonden en voor de game-exe gezet; hij bouwt de shim met de in /usr aanwezige gcc en schrijft
# die naar XDG_RUNTIME_DIR (schrijfbaar in de sandbox). Zie tools/rg-elfloader/.
if (( USE_LOADER )); then
  LOADERBIN="${RAVENGUN_ELFLOADER:-$DATA/bin/rg-elfloader}"
  [[ -x "$LOADERBIN" ]] || die "rg-elfloader niet gevonden ($LOADERBIN); bouw met scripts/build-elfloader.sh"
  BW+=(--ro-bind "$LOADERBIN" /rg/bin/rg-elfloader)
  GAMECMD=(/rg/bin/rg-elfloader "${GAMECMD[@]}")
fi

# Buiten gamescope draaien (tv of headless): dit script start zichzelf binnen gamescope en gaat dan verder als "inner".
if (( HEADLESS || GAMESCOPE )) && [[ -z "${RG_NATIVE_INNER:-}" ]]; then
  W="${RES%x*}"; H="${RES#*x}"
  SELF=("$0" --game "$GAME" --res "$RES")
  (( MUTE )) && SELF+=(--mute)
  (( DEBUG )) && SELF+=(--debug)
  (( GDB )) && SELF+=(--gdb)
  [[ -n "$INPUT_NODES" ]] && SELF+=(--input-nodes "$INPUT_NODES")
  [[ -n "$STRACE" ]] && SELF+=(--strace "$STRACE")
  for e in "${XENV[@]}"; do SELF+=(--env "$e"); done
  for ((i = 0; i < ${#BINDS[@]}; i += 2)); do SELF+=(--bind-ro "${BINDS[$i]}" "${BINDS[$((i+1))]}"); done
  for ((i = 0; i < ${#RW[@]}; i += 2)); do SELF+=(--bind-rw "${RW[$i]}" "${RW[$((i+1))]}"); done
  (( SHARE_IPC )) && SELF+=(--share-ipc)
  (( USE_LOADER )) && SELF+=(--loader)
  (( SECONDS_MAX )) && SELF+=(--seconds "$SECONDS_MAX")
  (( HEADLESS )) && SELF+=(--headless)
  (( GAMESCOPE )) && SELF+=(--gamescope)
  [[ -n "${V[res]}" ]] || true
  [[ ${#EXTRA[@]} -gt 0 ]] && SELF+=(-- "${EXTRA[@]}")
  if (( HEADLESS )); then
    GS=(gamescope --backend headless -W 1920 -H 1080 -w "$W" -h "$H")
  else
    GS=(gamescope -f -W 1920 -H 1080 -w "$W" -h "$H" --force-grab-cursor)
  fi
  (( DRY )) && { rg_identiteit "$0" gegevens="$JSON" >&2; echo "RG_NATIVE_INNER=1 ${GS[*]} -- ${SELF[*]}"; exit 0; }
  export RG_NATIVE_INNER=1
  if [[ -n "$SNAP" ]]; then
    ( "${GS[@]}" -- "${SELF[@]}" >"$LOGDIR/gamescope-$GAME.log" 2>&1 & echo $! >"$IPC/native-gs.pid"; wait ) &
    GSW=$!
    sleep $(( SECONDS_MAX > 6 ? SECONDS_MAX - 5 : 1 ))
    NODE=$(grep -o 'node ID: [0-9]*' "$LOGDIR/gamescope-$GAME.log" | tail -1 | grep -o '[0-9]*$' || true)
    [[ -n "$NODE" ]] && timeout 20 gst-launch-1.0 -q pipewiresrc path="$NODE" num-buffers=3 ! videoconvert ! pngenc snapshot=true ! filesink location="$SNAP" >/dev/null 2>&1 || true
    wait "$GSW" || true
    exit 0
  fi
  exec "${GS[@]}" -- "${SELF[@]}"
fi

XNUM=""
if [[ -n "${DISPLAY:-}" ]]; then XNUM="${DISPLAY#:}"; XNUM="${XNUM%%.*}"; fi
[[ -n "$XNUM" ]] || die "DISPLAY niet vast te stellen (Desktop Mode actief?)"
BW+=(--dir /tmp/.X11-unix --ro-bind "/tmp/.X11-unix/X$XNUM" "/tmp/.X11-unix/X$XNUM")
if [[ -n "${XAUTHORITY:-}" && -r "${XAUTHORITY}" ]]; then BW+=(--ro-bind "$XAUTHORITY" /tmp/.Xauthority --setenv XAUTHORITY /tmp/.Xauthority); fi
BW+=(--dir "$RUNDIR" --chmod 0700 "$RUNDIR")
if (( ! MUTE && ! HEADLESS )); then
  BW+=(--ro-bind-try "$RUNDIR/pulse" "$RUNDIR/pulse" --ro-bind-try "$RUNDIR/pipewire-0" "$RUNDIR/pipewire-0" --setenv PULSE_SERVER "unix:$RUNDIR/pulse/native")
fi

BW+=(--chdir "${V[cwd]:-/}" --clearenv
  --setenv HOME "${V[home]:-/tmp}" --setenv PATH /usr/bin
  --setenv DISPLAY "$DISPLAY" --setenv XDG_RUNTIME_DIR "$RUNDIR" --setenv LANG C.UTF-8)
[[ -n "$LDPATH" ]] && BW+=(--setenv LD_LIBRARY_PATH "$LDPATH")
[[ -n "${V[preload]}" && -z "$STRACE" && $GDB -eq 0 ]] && BW+=(--setenv LD_PRELOAD "${V[preload]}")
(( DEBUG )) && BW+=(--setenv LD_DEBUG libs)
for e in "${GENV[@]}" "${XENV[@]}"; do BW+=(--setenv "${e%%=*}" "${e#*=}"); done

if [[ -n "$STRACE" ]]; then
  # strace zelf draait zonder preload (anders hookt halo.so ook in strace); de preload gaat alleen naar het proces van de game (-E).
  command -v strace >/dev/null || die "strace niet gevonden"
  PRE=(); [[ -n "${V[preload]}" ]] && PRE=(-E "LD_PRELOAD=${V[preload]}")
  BW+=(--bind "$(dirname "$STRACE")" /rg-strace)
  GAMECMD=(/usr/bin/strace -f -tt -s 200 -o "/rg-strace/$(basename "$STRACE")" -e trace=openat,open,ioctl,connect,execve "${PRE[@]}" -- "${GAMECMD[@]}")
fi

if (( GDB )); then
  command -v gdb >/dev/null || die "gdb niet gevonden"
  GENVS=(); [[ -n "${V[preload]}" ]] && GENVS=(-ex "set environment LD_PRELOAD ${V[preload]}")
  GAMECMD=(/usr/bin/gdb -batch -ex "set debuginfod enabled off" -ex "set pagination off" -ex "set startup-with-shell off" "${GENVS[@]}" ${RG_GDB_CMDS:+-x "$RG_GDB_CMDS"} -ex run -ex bt -ex "info registers" -ex "x/8i \$pc" --args "${GAMECMD[@]}")
fi

# Wie voert deze run uit: dit script, het gegevensbestand, en wat er van ons de sandbox in gaat (de eigen lader, onze eigen
# bibliotheken uit build/ of lib/, en bestanden die met --bind-ro zijn meegegeven). Een oude kopie daarvan is een andere meting;
# daarom staat het bovenaan het logboek van elke run (en bij --dry-run op stderr, zodat stdout het commando blijft).
WIE=(gegevens="$JSON" hulp="$ROOT/scripts/rg-paths.sh" hulp="$ROOT/scripts/rg-identiteit.sh")
if [[ -n "${LOADERBIN:-}" ]]; then WIE+=(lader="$LOADERBIN"); fi
for l in "${LIBS[@]}"; do if [[ "$l" == "$RG_LIB"/* ]]; then WIE+=(bibliotheek="$l"); fi; done
for ((i = 0; i < ${#BINDS[@]}; i += 2)); do
  [[ -f "${BINDS[$i]}" ]] || continue
  if [[ "${BINDS[$((i+1))]}" == "$RT_TABEL_DOEL" ]]; then WIE+=(invoertabel="${BINDS[$i]}"); else WIE+=(meegegeven="${BINDS[$i]}"); fi
done
IDENT=$(rg_identiteit "$0" "${WIE[@]}") || IDENT="script: $0 (identiteit niet vast te stellen)"

if (( DRY )); then echo "$IDENT" >&2; echo "$CAMERA_REGEL" >&2; printf '%q ' "${BW[@]}" "${GAMECMD[@]}"; echo; exit 0; fi

# Eén launcher tegelijk.
exec 9>"$IPC/native.lock"
flock -n 9 || die "er draait al een native launcher (zie $IPC/native.pid); stop die met --stop"
echo $$ > "$IPC/native.pid"
TS=$(date +%Y%m%d-%H%M%S); LOG="$LOGDIR/native-$GAME-$TS.log"
{ echo "== play-native $GAME $TS"; echo "$IDENT"; echo "$CAMERA_REGEL"; echo "display=$DISPLAY res=$RES headless=$HEADLESS preload=${V[preload]:-geen}"; echo "cmd: ${GAMECMD[*]}"; } >>"$LOG"
echo "play-native: $GAME (${V[title]}), res $RES, log $LOG"
[[ "${V[status]}" == werkt ]] || echo "play-native: LET OP: status ${V[status]} (${V[note]})" >&2

CHILD=""
cleanup() { [[ -n "$CHILD" ]] && kill -TERM "$CHILD" 2>/dev/null || true; }
trap cleanup INT TERM
set +e
"${BW[@]}" "${GAMECMD[@]}" >>"$LOG" 2>&1 &
CHILD=$!
if (( SECONDS_MAX > 0 )); then
  ( sleep "$SECONDS_MAX"; kill -TERM "$CHILD" 2>/dev/null ) &
  TIMER=$!
fi
wait "$CHILD"; RC=$?
[[ -n "${TIMER:-}" ]] && kill "$TIMER" 2>/dev/null
set -e
echo "== exit $RC" >>"$LOG"
rm -f "$IPC/native.pid"
echo "play-native: $GAME beëindigd met exitcode $RC (log: $LOG)"
exit "$RC"
