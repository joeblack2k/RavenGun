#!/usr/bin/env bash
# scripts/rg-tv-env.sh: de schermomgeving voor een run met beeld op de tv. Hermetisch: de "sessie" is een eigen
# slaapproces met een bekende omgeving (RG_TV_ENV_FROM_PID) en xrandr is een nepje op het PATH. Er wordt geen scherm
# aangeraakt en niets gestart.
# Wat hier wordt afgedwongen: de variabelen komen heel door (ook met een spatie in een pad), een commando krijgt ze
# mee, en ZONDER sessie of met een onbereikbaar scherm stopt het script luid in plaats van het commando toch te starten.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
S="$ROOT/scripts/rg-tv-env.sh"
T=$(mktemp -d) || exit 1
FAILS=0
fail() { echo "FAIL: $*"; FAILS=$((FAILS + 1)); }
SESPID=""
trap '[[ -n "$SESPID" ]] && kill "$SESPID" 2>/dev/null; rm -rf "$T"' EXIT

mkdir -p "$T/bin" "$T/run dir"
: > "$T/run dir/xauth"
nep_xrandr() {   # $1 = exitcode
  printf '#!/bin/sh\n[ %s -eq 0 ] && echo "NEP-1 connected primary 1920x1080+0+0 (normal) 1mm x 1mm" || echo "Authorization required, but no authorization protocol specified" >&2\nexit %s\n' "$1" "$1" > "$T/bin/xrandr"
  chmod +x "$T/bin/xrandr"
}
sessie() {       # start een "sessieproces" met de gegeven omgeving; zet SESPID
  [[ -n "$SESPID" ]] && kill "$SESPID" 2>/dev/null
  env -i PATH="$PATH" "$@" sleep 60 &
  SESPID=$!
  sleep 0.2
}
tv() { RG_TV_ENV_FROM_PID="$SESPID" PATH="$T/bin:$PATH" "$S" "$@"; }

# 1. De variabelen van de sessie komen heel door, ook een pad met een spatie.
nep_xrandr 0
sessie DISPLAY=:77 "XAUTHORITY=$T/run dir/xauth" WAYLAND_DISPLAY=wayland-9 "XDG_RUNTIME_DIR=$T/run dir" ANDERS=nee
UIT=$(tv) || fail "zonder optie: exitcode $?"
( unset DISPLAY XAUTHORITY WAYLAND_DISPLAY XDG_RUNTIME_DIR ANDERS
  eval "$UIT"
  [[ "$DISPLAY" == ":77" && "$XAUTHORITY" == "$T/run dir/xauth" && "$WAYLAND_DISPLAY" == "wayland-9" \
     && "$XDG_RUNTIME_DIR" == "$T/run dir" && -z "${ANDERS:-}" ]] ) || fail "de export-regels kloppen niet: $UIT"

# 2. --check meldt het scherm en geeft dezelfde regels.
ERR=$(tv --check 2>&1 >/dev/null) || fail "--check: exitcode $?"
grep -q "scherm :77 bereikbaar" <<<"$ERR" || fail "--check noemt het scherm niet: $ERR"

# 3. -- CMD: het commando krijgt de omgeving, en zijn exitcode komt terug.
UIT=$(tv -- sh -c 'echo "$DISPLAY|$XAUTHORITY|$WAYLAND_DISPLAY"; exit 7' 2>/dev/null); RC=$?
[[ $RC -eq 7 ]] || fail "-- CMD: de exitcode van het commando komt niet terug ($RC)"
[[ "$UIT" == ":77|$T/run dir/xauth|wayland-9" ]] || fail "-- CMD: de omgeving klopt niet: $UIT"

# 3b. -- CMD neemt het scherm: er staat een houder met het pid van het commando zelf (het commando neemt het proces over), met
#     de starttijd van dat proces erin, zolang het commando loopt. Met --no-hold niet.
HD="$T/run dir/ravengun/tv-hold"
rm -rf "$HD"      # de houder van stap 3 staat er nog (dat proces is weg; het bord ruimt hem op, dit script niet)
UIT=$(tv -- sh -c 'echo "$$"; ls "$XDG_RUNTIME_DIR/ravengun/tv-hold"; head -1 "$XDG_RUNTIME_DIR/ravengun/tv-hold/$$"; awk "{print \$22}" /proc/$$/stat' 2>/dev/null)
PIDC=$(sed -n 1p <<<"$UIT")
[[ -n "$PIDC" && "$(sed -n 2p <<<"$UIT")" == "$PIDC" && "$(wc -l <<<"$UIT")" -eq 4 ]] || fail "-- CMD: geen houder (precies een) met het pid van het commando: $UIT"
[[ "$(sed -n 3p <<<"$UIT")" == "$(sed -n 4p <<<"$UIT")" && "$(sed -n 3p <<<"$UIT")" != 0 ]] || fail "-- CMD: de houder noemt niet de starttijd van het proces: $UIT"
rm -rf "$HD"
UIT=$(tv --no-hold -- sh -c 'ls "$XDG_RUNTIME_DIR/ravengun/tv-hold" 2>/dev/null | wc -l; exit 5' 2>/dev/null); RC=$?
[[ $RC -eq 5 && "$UIT" == 0 ]] || fail "--no-hold: exitcode $RC, houders: $UIT"
"$S" --no-hold >/dev/null 2>&1 && fail "--no-hold zonder -- CMD wordt geaccepteerd"

# 4. Een scherm dat de verbinding weigert: luid stoppen, het commando NIET starten.
nep_xrandr 1
ERR=$(tv -- sh -c "touch '$T/gestart'" 2>&1); RC=$?
[[ $RC -eq 1 ]] || fail "onbereikbaar scherm: exitcode $RC, verwacht 1"
grep -q "niet bereikbaar" <<<"$ERR" || fail "onbereikbaar scherm: geen melding ($ERR)"
[[ ! -e "$T/gestart" ]] || fail "onbereikbaar scherm: het commando is toch gestart"
tv --check >/dev/null 2>&1 && fail "--check slaagt bij een onbereikbaar scherm"
nep_xrandr 0

# 5. Een sessie zonder DISPLAY: stoppen.
sessie WAYLAND_DISPLAY=wayland-9
ERR=$(tv -- sh -c "touch '$T/gestart'" 2>&1); RC=$?
[[ $RC -eq 1 && ! -e "$T/gestart" ]] || fail "sessie zonder DISPLAY: exitcode $RC of toch gestart"
grep -q "geen DISPLAY" <<<"$ERR" || fail "sessie zonder DISPLAY: geen melding ($ERR)"

# 6. Een XAUTHORITY die er niet is: stoppen (het scherm zou de verbinding weigeren).
sessie DISPLAY=:77 "XAUTHORITY=$T/bestaat-niet"
ERR=$(tv 2>&1); RC=$?
[[ $RC -eq 1 ]] || fail "onleesbare XAUTHORITY: exitcode $RC, verwacht 1"
grep -q "niet leesbaar" <<<"$ERR" || fail "onleesbare XAUTHORITY: geen melding ($ERR)"

# 7. Geen sessie: stoppen.
kill "$SESPID" 2>/dev/null; wait "$SESPID" 2>/dev/null; DOOD=$SESPID; SESPID=""
ERR=$(RG_TV_ENV_FROM_PID="$DOOD" PATH="$T/bin:$PATH" "$S" -- sh -c "touch '$T/gestart'" 2>&1); RC=$?
[[ $RC -eq 1 && ! -e "$T/gestart" ]] || fail "geen sessie: exitcode $RC of toch gestart"
grep -q "geen bureaublad- of spelsessie" <<<"$ERR" || fail "geen sessie: geen melding ($ERR)"

# 8. Verkeerd gebruik.
"$S" --bestaat-niet >/dev/null 2>&1 && fail "een onbekende optie wordt geaccepteerd"
"$S" -- >/dev/null 2>&1 && fail "-- zonder commando wordt geaccepteerd"

[[ $FAILS -eq 0 ]] || { echo "$FAILS fout(en)"; exit 1; }
echo "ok: rg-tv-env geeft de schermomgeving door en stopt luid zonder scherm"
