#!/usr/bin/env bash
# RavenGun: geef een run de omgeving van het scherm (de tv), zodat een spel met beeld draait en niet onzichtbaar.
#
# Een run die vanuit een dienst, ssh of tmux start heeft geen DISPLAY en geen XAUTHORITY. Het spel krijgt dan geen
# venster: de X-server van het bureaublad weigert de verbinding ("Authorization required") of er is helemaal geen
# scherm bekend. Dit script leest de schermomgeving uit de lopende bureaubladsessie en geeft die door.
#
# Gebruik:
#   tools/dev/rg-tv-env.sh                 de export-regels (voor: eval "$(tools/dev/rg-tv-env.sh)")
#   tools/dev/rg-tv-env.sh --check         hetzelfde, en controleer dat het scherm echt bereikbaar is (xrandr)
#   tools/dev/rg-tv-env.sh -- CMD [ARG..]  start CMD met die omgeving, en NEEM HET SCHERM: het statusbord gaat weg en blijft weg
#                                        zolang CMD loopt (zie hieronder)
#   tools/dev/rg-tv-env.sh --no-hold -- CMD  hetzelfde zonder het scherm te nemen (een schermafbeelding van het bord zelf)
#
# HET SCHERM NEMEN. Het statusbord (scripts/oled-saver.sh) is het rustscherm van de tv. Een run die hierlangs start heeft
# beeld, dus het bord moet wijken -- anders ligt het bord over het spel en meet je het bord (gebeurd op 2026-10-13: acht
# identieke opnames van de achtergrond van het bord). Daarom schrijft dit script voor de start een houder
# ($XDG_RUNTIME_DIR/ravengun/tv-hold/<pid>, met de starttijd van het proces erin) en haalt het bord meteen weg. CMD neemt dit
# proces over (exec), dus de houder geldt precies zolang CMD leeft; daarna ziet het bord dat het proces weg is, ruimt de
# houder op en komt na zijn gewone wachttijd terug. Er is geen stap om te vergeten en niets om op te ruimen.
#
# Wordt er geen sessie gevonden, of is het scherm niet bereikbaar (--check en --), dan stopt het script met exitcode 1
# en een melding. Het gaat nooit stil door: een run waarvan je denkt dat hij op de tv staat terwijl hij onzichtbaar
# draait, is een meting onder een andere opzet dan je denkt.
#
# Het script start zelf niets en verandert niets; het leest /proc/<pid>/environ en /proc/<pid>/cmdline van onze eigen
# gebruiker. RG_TV_ENV_FROM_PID=<pid> kiest het proces waaruit gelezen wordt (voor de test).
set -euo pipefail

VARS=(DISPLAY XAUTHORITY WAYLAND_DISPLAY XDG_RUNTIME_DIR DBUS_SESSION_BUS_ADDRESS)
die() { echo "rg-tv-env: $*" >&2; exit 1; }

MODE=print; CMD=(); HOLD=1
if [[ "${1:-}" == --no-hold ]]; then HOLD=0; shift; [[ "${1:-}" == -- ]] || die "--no-hold hoort voor -- CMD"; fi
case "${1:-}" in
  "") ;;
  --check) MODE=check ;;
  --) shift; [[ $# -gt 0 ]] || die "na -- hoort een commando"; MODE=run; CMD=("$@") ;;
  -h|--help) sed -n '2,28p' "$0"; exit 0 ;;
  *) die "onbekende optie: $1 (zie --help)" ;;
esac

# Het proces waaruit we lezen: een proces van de bureaubladsessie (Desktop Mode) of van de spelsessie (Game Mode).
SRC_PID="${RG_TV_ENV_FROM_PID:-}"; SRC_NAME="RG_TV_ENV_FROM_PID"
if [[ -z "$SRC_PID" ]]; then
  for name in plasmashell kwin_wayland steam; do
    SRC_PID=$(pgrep -u "$(id -u)" -x "$name" | head -1 || true)
    [[ -n "$SRC_PID" ]] && { SRC_NAME="$name"; break; }
  done
fi
[[ -n "$SRC_PID" && -r "/proc/$SRC_PID/environ" ]] \
  || die "geen bureaublad- of spelsessie gevonden (plasmashell, kwin_wayland, steam); een run zou zonder beeld draaien"

declare -A E=()
while IFS= read -r -d '' kv; do
  k="${kv%%=*}"
  for v in "${VARS[@]}"; do [[ "$k" == "$v" ]] && E[$k]="${kv#*=}"; done
done < "/proc/$SRC_PID/environ"

# kwin_wayland kent zijn eigen X-scherm alleen als argument, niet als omgevingsvariabele.
KW=$(pgrep -u "$(id -u)" -x kwin_wayland | head -1 || true)
if [[ -z "${RG_TV_ENV_FROM_PID:-}" && -n "$KW" && -r "/proc/$KW/cmdline" ]]; then
  mapfile -d '' -t A < "/proc/$KW/cmdline"
  for (( i = 0; i < ${#A[@]} - 1; i++ )); do
    case "${A[$i]}" in
      --xwayland-display)    [[ -n "${E[DISPLAY]:-}" ]]    || E[DISPLAY]="${A[$((i+1))]}" ;;
      --xwayland-xauthority) [[ -n "${E[XAUTHORITY]:-}" ]] || E[XAUTHORITY]="${A[$((i+1))]}" ;;
      --socket)              [[ -n "${E[WAYLAND_DISPLAY]:-}" ]] || E[WAYLAND_DISPLAY]="${A[$((i+1))]}" ;;
    esac
  done
fi
[[ -n "${E[XDG_RUNTIME_DIR]:-}" ]] || E[XDG_RUNTIME_DIR]="/run/user/$(id -u)"

[[ -n "${E[DISPLAY]:-}" ]] || die "de sessie ($SRC_NAME, pid $SRC_PID) heeft geen DISPLAY; een run zou zonder beeld draaien"
if [[ -n "${E[XAUTHORITY]:-}" && ! -r "${E[XAUTHORITY]}" ]]; then
  die "XAUTHORITY ${E[XAUTHORITY]} van de sessie is niet leesbaar; het scherm zou de verbinding weigeren"
fi

bereikbaar() {   # vraagt het scherm naar zijn uitgangen; opent geen venster
  command -v xrandr >/dev/null || { echo "rg-tv-env: xrandr ontbreekt; bereikbaarheid niet gecontroleerd" >&2; return 0; }
  local uit
  if ! uit=$(env DISPLAY="${E[DISPLAY]}" ${E[XAUTHORITY]:+XAUTHORITY="${E[XAUTHORITY]}"} timeout 5 xrandr --query 2>&1); then
    die "het scherm ${E[DISPLAY]} is niet bereikbaar: ${uit%%$'\n'*}"
  fi
  echo "rg-tv-env: scherm ${E[DISPLAY]} bereikbaar (bron: $SRC_NAME, pid $SRC_PID): $(grep -m1 ' connected' <<<"$uit" | cut -d'(' -f1)" >&2
}

case "$MODE" in
  print|check)
    [[ "$MODE" == check ]] && bereikbaar
    for v in "${VARS[@]}"; do
      if [[ -n "${E[$v]:-}" ]]; then printf 'export %s=%q\n' "$v" "${E[$v]}"; fi
    done
    ;;
  run)
    bereikbaar
    ENVV=()
    for v in "${VARS[@]}"; do
      if [[ -n "${E[$v]:-}" ]]; then ENVV+=("$v=${E[$v]}"); fi
    done
    if (( HOLD )); then
      # De houder: ons pid (CMD neemt dit proces over) en de starttijd, zodat een later proces met hetzelfde pid niet telt.
      HD="${E[XDG_RUNTIME_DIR]}/ravengun/tv-hold"
      if mkdir -p "$HD" 2>/dev/null; then
        { awk '{print $22}' "/proc/$$/stat" 2>/dev/null || echo 0; date +%H:%M:%S; printf '%s ' "${CMD[@]}"; echo; } > "$HD/$$" 2>/dev/null \
          || echo "rg-tv-env: LET OP: de houder $HD/$$ is niet te schrijven; het statusbord kan over het spel heen liggen" >&2
      else
        echo "rg-tv-env: LET OP: $HD is niet te maken; het statusbord kan over het spel heen liggen" >&2
      fi
      # Het bord meteen weg, niet pas bij zijn volgende ronde (om de 3 s): anders liggen de eerste seconden van de run eronder.
      SAVER="$(cd "$(dirname "$0")/../../scripts" && pwd)/oled-saver.sh"
      if [[ -x "$SAVER" && -z "${RG_TV_ENV_FROM_PID:-}" ]]; then env XDG_RUNTIME_DIR="${E[XDG_RUNTIME_DIR]}" "$SAVER" --stop-view >/dev/null 2>&1 || true; fi
    fi
    exec env "${ENVV[@]}" "${CMD[@]}"
    ;;
esac
