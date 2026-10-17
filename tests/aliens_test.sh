#!/usr/bin/env bash
# rg-aliens-hook.dll (tools/rg-aliens/): bouwt de eenheidstest van de logica (tests/aliens_unit.c) en draait hem. Daarna:
#  - de indeling in de hook (bits van de besturing) moet gelijk zijn aan packaging/tp-shm-profiles.json, profiel AliensExtermination;
#  - de DLL zelf moet bouwen met de mingw-sysroot (overgeslagen als die er niet is), zonder waarschuwingen en met alleen kernel32 en user32;
#  - als de exe van de game bereikbaar is (NAS): de bytes van de twee getters kloppen met wat de hook verwacht.
# Skip (77) zonder C-compiler of python3.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="${CC:-cc}"; command -v "$CC" >/dev/null || { echo "overgeslagen: geen C-compiler"; exit 77; }
command -v python3 >/dev/null || { echo "overgeslagen: geen python3"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
"$CC" -O1 -g -Wall -Wextra -Werror -o "$T/aliens_unit" "$ROOT/tests/aliens_unit.c" 2>"$T/cc.err" \
  || { cat "$T/cc.err"; echo "FAIL: de eenheidstest bouwt niet"; exit 1; }
"$T/aliens_unit" || exit 1
EXE="${RG_ALIENS_EXE:-$HOME/mnt/spidernas_emulation/Teknoparrot/Aliens Extermination/DATA/aliens dehasped.exe}"
[[ -r "$EXE" ]] && { "$T/aliens_unit" --exe "$EXE" || exit 1; }

# de indeling tegen het JSON-bestand
"$T/aliens_unit" --dump > "$T/hook.txt"
python3 -I - "$ROOT/packaging/tp-shm-profiles.json" "$T/hook.txt" <<'PY' || exit 1
import json, sys
prof = json.load(open(sys.argv[1]))["profiles"]["AliensExtermination"]
want = {b["signal"]: 1 << b["bit"] for b in prof["bits"] if b["offset"] == 8}
have = {}
for line in open(sys.argv[2]):
    k, v = line.split()
    have[k] = int(v, 16)
bad = [(k, hex(have[k]), hex(want.get(k, 0))) for k in have if want.get(k) != have[k]]
if bad:
    print("FAIL: de hook en packaging/tp-shm-profiles.json (AliensExtermination) lopen uit de pas:", bad)
    sys.exit(1)
ax = {a["signal"]: (a["offset"], a["bytes"]) for a in prof["axes"]}
if ax.get("p1.x") != (12, 1) or ax.get("p1.y") != (16, 1) or ax.get("p2.x") != (20, 1) or ax.get("p2.y") != (24, 1):
    print("FAIL: de asbytes (12/16/20/24) kloppen niet meer met het profiel:", ax)
    sys.exit(1)
print("aliens: indeling gelijk aan tp-shm-profiles.json (%d bits, 4 assen)" % len(have))
PY

# de DLL zelf
SR="${RG_MINGW_SYSROOT:-$HOME/.cache/ravengun-mingw/sysroot}"
if [[ -x "$SR/usr/bin/i686-w64-mingw32-gcc" ]]; then
  bash "$ROOT/tools/rg-aliens/build.sh" "$T/rg-aliens-hook.dll" >"$T/build.out" 2>&1 \
    || { cat "$T/build.out"; echo "FAIL: rg-aliens-hook.dll bouwt niet"; exit 1; }
  grep -qi "warning" "$T/build.out" && { cat "$T/build.out"; echo "FAIL: waarschuwingen bij het bouwen van de DLL"; exit 1; }
  imps=$(objdump -p "$T/rg-aliens-hook.dll" | awk '/DLL Name/ {print tolower($3)}' | sort | tr '\n' ' ')
  [[ "$imps" == "kernel32.dll user32.dll " ]] || { echo "FAIL: de DLL importeert $imps (alleen kernel32 en user32 verwacht)"; exit 1; }
  echo "aliens: DLL bouwt, imports: $imps"
else
  echo "aliens: mingw-sysroot ontbreekt, DLL-bouw overgeslagen"
fi

# de echte exe, als hij er is (NAS): de getters
EXE="${RG_ALIENS_EXE:-$HOME/mnt/spidernas_emulation/Teknoparrot/Aliens Extermination/DATA/aliens dehasped.exe}"
if [[ -r "$EXE" ]]; then
  python3 -I - "$EXE" <<'PY' || exit 1
import sys
f = open(sys.argv[1], "rb")
def at(rva):
    f.seek(0x1000 + rva - 0x1000)          # .text: RVA 0x1000 staat op bestandsoffset 0x1000
    return f.read(6)
ok = at(0x2ed00) == bytes.fromhex("a188ae2705c3") and at(0x2ed10) == bytes.fromhex("a18cae2705c3")
if not ok:
    print("FAIL: de getters in de exe hebben andere bytes dan de hook verwacht (andere build?)")
    sys.exit(1)
print("aliens: getters in de exe kloppen")
PY
else
  echo "aliens: exe niet bereikbaar, bytecontrole overgeslagen"
fi
