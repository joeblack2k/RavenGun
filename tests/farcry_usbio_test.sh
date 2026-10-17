#!/usr/bin/env bash
# Far Cry: Paradise Lost, onze Usbioextreme.dll (tools/rg-farcry): de eenheidstest van de rekenregel (tests/farcry_usbio_unit.c) en,
# als de mingw-keten er is, een bouwproef van de DLL met een controle op de uitvoer: dezelfde 29 namen op dezelfde ordinalen als het
# origineel (Far Cry's Usbioextreme.dll, gemeten) en een DllMain. Skip (77) zonder C-compiler.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="${CC:-cc}"; command -v "$CC" >/dev/null || { echo "overgeslagen: geen C-compiler"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
"$CC" -O1 -g -Wall -Wextra -o "$T/unit" "$ROOT/tests/farcry_usbio_unit.c" 2>"$T/cc.err" \
  || { cat "$T/cc.err"; echo "FAIL: de eenheidstest bouwt niet"; exit 1; }
[[ ! -s "$T/cc.err" ]] || { cat "$T/cc.err"; echo "FAIL: waarschuwingen bij het bouwen van de test"; exit 1; }
"$T/unit" || exit 1
# de DLL zelf (alleen als de eigen mingw-keten er is; anders is dit stuk overgeslagen, niet mislukt)
SR="${RG_MINGW_SYSROOT:-$HOME/.cache/ravengun-mingw/sysroot}"
if [[ -x "$SR/usr/bin/i686-w64-mingw32-gcc" ]] && command -v objdump >/dev/null; then
  "$ROOT/tools/rg-farcry/build.sh" "$T/Usbioextreme.dll" >"$T/build.out" 2>&1 || { cat "$T/build.out"; echo "FAIL: de DLL bouwt niet"; exit 1; }
  objdump -p "$T/Usbioextreme.dll" | sed -n '/Ordinal\/Name Pointer/,/^$/p' | grep -E '^\s+\[' > "$T/exports.txt"
  n=$(wc -l < "$T/exports.txt")
  [[ "$n" -eq 29 ]] || { cat "$T/exports.txt"; echo "FAIL: $n exports in plaats van 29"; exit 1; }
  # de zes namen die Farcry_R.exe importeert (objdump -p Farcry_R.exe), met hun ordinaal in het origineel
  for want in '1 ??0CUSBIO@@QAE@XZ' '2 ??1CUSBIO@@QAE@XZ' '5 ?CUSBIO_Close@CUSBIO@@QAEXXZ' '6 ?CUSBIO_GetLastError@CUSBIO@@QAEPADH@Z' \
              '7 ?CUSBIO_Init@CUSBIO@@QAEHXZ' '8 ?CUSBIO_Update@CUSBIO@@QAEXH@Z'; do
    ord=${want%% *}; name=${want#* }
    grep -qF -- "+base[$(printf '%4d' "$ord")]" "$T/exports.txt" && grep -F -- "+base[$(printf '%4d' "$ord")]" "$T/exports.txt" | grep -qF -- " $name" \
      || { echo "FAIL: export $name op ordinaal $ord ontbreekt"; exit 1; }
  done
  # de aangeroepene ruimt de stapel op zoals het origineel: Update en GetLastError "ret 4", Init en Close "ret"
  objdump -d -M intel "$T/Usbioextreme.dll" | grep -qE 'ret +0x4$' || { echo "FAIL: geen 'ret 0x4' in de DLL (Update/GetLastError)"; exit 1; }
  echo "farcry_usbio_dll: ok (29 exports)"
else
  echo "overgeslagen: geen mingw-keten (alleen de eenheidstest gedraaid)"
fi
