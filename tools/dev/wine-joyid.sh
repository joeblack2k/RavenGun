#!/usr/bin/env bash
# Zorgt dat in een Wine-prefix een bepaald apparaat joystick-id 0 heeft. Een game die DirectInput GUID_Joystick opent
# (Transformers: Shadows Rising) krijgt de joystick met id 0; zonder dit is dat de PS Move zelf (die Wine via hidraw ziet)
# of een Steam-controller. Wine bewaart de ids per product in HKCU\System\CurrentControlSet\Control\MediaProperties\
# PrivateProperties\DirectInput\VID_xxxx&PID_xxxx\Calibration\N ("Joystick Id").
#
# Gebruik: tools/dev/wine-joyid.sh PREFIX VID:PID      (hex, bv. 1209:72F0; PREFIX = WINEPREFIX)
# Raakt alleen de sleutels "Joystick Id" aan; andere apparaten met id 0 krijgen het eerstvolgende vrije id. Draai het niet
# terwijl een game in dezelfde prefix draait (het stopt de wineserver van de prefix niet).
set -euo pipefail
PREFIX="${1:?PREFIX vereist}"; VIDPID="${2:?VID:PID vereist}"
WINE="${RG_WINE:-$HOME/.local/share/ravengun/wine/proton-11.0-2/bin/wine}"
export WINEPREFIX="$PREFIX" WINEDEBUG=-all
VID="${VIDPID%%:*}"; PID="${VIDPID##*:}"
OURS="VID_${VID^^}&PID_${PID^^}"
BASE='HKCU\System\CurrentControlSet\Control\MediaProperties\PrivateProperties\DirectInput'
[[ -d "$PREFIX" ]] || { echo "prefix ontbreekt: $PREFIX" >&2; exit 1; }
# Huidige ids lezen: regels "<sleutel>" en "Joystick Id REG_BINARY 05000000".
declare -A ID
key=""
while IFS= read -r line; do
  line="${line%$'\r'}"
  if [[ "$line" == HKEY_CURRENT_USER*Calibration* ]]; then key="${line##*DirectInput\\}"; fi
  if [[ -n "$key" && "$line" =~ Joystick\ Id.*REG_BINARY[[:space:]]+([0-9A-Fa-f]{2}) ]]; then ID["$key"]=$((16#${BASH_REMATCH[1]})); key=""; fi
done < <("$WINE" reg query "$BASE" /s 2>/dev/null || true)
used=" "
for k in "${!ID[@]}"; do [[ "$k" == "$OURS"* ]] || used+="${ID[$k]} "; done
changed=0
setid() { "$WINE" reg add "$BASE\\$1" /v "Joystick Id" /t REG_BINARY /d "$(printf '%02x000000' "$2")" /f >/dev/null 2>&1; changed=1; }
for k in "${!ID[@]}"; do
  if [[ "$k" != "$OURS"* && "${ID[$k]}" == 0 ]]; then
    n=1; while [[ "$used" == *" $n "* ]]; do n=$((n+1)); done
    used+="$n "; setid "$k" "$n"; echo "joystick-id van $k: 0 -> $n"
  fi
done
have=0; for k in "${!ID[@]}"; do [[ "$k" == "$OURS"* ]] && have=1 && [[ "${ID[$k]}" != 0 ]] && { setid "$k" 0; echo "joystick-id van $k: ${ID[$k]} -> 0"; }; done
if [[ $have -eq 0 ]]; then setid "$OURS\\Calibration\\0" 0; echo "joystick-id van $OURS vastgelegd: 0"; fi
[[ $changed -eq 1 ]] && { WINESERVER="$(dirname "$WINE")/wineserver"; "$WINESERVER" -w 2>/dev/null || true; }
exit 0
