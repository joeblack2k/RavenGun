#!/bin/bash
# The core proof: what does the arithmetic repair do, per game, WITHOUT the bench?
#
# For each game we build the hook twice -- once from the current source (new arithmetic:
# hh_base - hh_laag + st_value) and once from a copy with the OLD arithmetic (hh_base + st_value) --
# and run both against a real mapped image. The name route is the only route involved.
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
DATA=~/.local/share/ravengun
N=/home/deck/mnt/spidernas_emulation/Teknoparrot
TMP=$(mktemp -d)
INC=()
[ -e /usr/include/gnu/stubs-32.h ] || INC=(-I"$R/hasp/src/inc")

SRC="$R/tools/rt-hasphook/hasp_inline_hook.c"
NIEUW='adres = (uintptr_t)(hh_base - (uintptr_t)hh_laag) + (uintptr_t)hh_symtab[i].st_value;'
OUD='adres = hh_base + (uintptr_t)hh_symtab[i].st_value;'

echo "############ 0. de twee varianten van dezelfde bron"
grep -c -F "$NIEUW" "$SRC" | sed 's/^/  nieuwe rekenwijze gevonden: /'
# Let op: met sed gaat dit mis (de [i] in hh_symtab[i] is een bracket-expressie), dus python.
python3 - "$SRC" "$TMP/oud.c" "$NIEUW" "$OUD" <<'PY'
import sys, io
src, dst, nieuw, oud = sys.argv[1:5]
s = io.open(src, encoding="utf-8", errors="replace").read()
if s.count(nieuw) != 1:
    print("  FOUT: de nieuwe regel komt %d keer voor" % s.count(nieuw)); sys.exit(1)
io.open(dst, "w", encoding="utf-8", newline="\n").write(s.replace(nieuw, oud))
print("  oude variant gemaakt (alleen die ene regel teruggedraaid)")
PY
if grep -q -F "$OUD" "$TMP/oud.c" && ! grep -q -F "$NIEUW" "$TMP/oud.c"; then
  echo "  controle: oude variant klopt"
else
  echo "  FOUT: kon de oude variant niet maken"; exit 1
fi

echo
echo "############ 1. de vier bouwen"
gcc -m32 "${INC[@]}" -shared -fPIC -O2 -w -o "$TMP/hook32-nieuw.so" "$SRC" -ldl -lpthread || exit 1
gcc -m32 "${INC[@]}" -shared -fPIC -O2 -w -o "$TMP/hook32-oud.so"   "$TMP/oud.c" -ldl -lpthread || exit 1
gcc -shared -fPIC -O2 -w -o "$TMP/hook64-nieuw.so" "$SRC" -ldl -lpthread || exit 1
gcc -shared -fPIC -O2 -w -o "$TMP/hook64-oud.so"   "$TMP/oud.c" -ldl -lpthread || exit 1
for f in "$TMP"/hook*.so; do printf '  %-24s %8s bytes  %s\n' "$(basename "$f")" "$(stat -c%s "$f")" "$(file -b "$f" | cut -c1-40)"; done

gcc -m32 "${INC[@]}" -Wall -O2 -o "$TMP/test32" "$P/probe/hook-name-route-test.c" -ldl || exit 1
gcc -Wall -O2 -o "$TMP/test64" "$P/probe/hook-name-route-test.c" -ldl || exit 1
echo "  testprogramma's gebouwd"

# De elf doelen die de hook kent, met hun gemeten st_value uit de dynsym van Jurassic Park.
JP_ADR="0x08529650 0x085282b0 0x085295f0 0x08528520 0x08528470 0x08529550 0x085294d0 0x085283d0 0x08528090 0x08528670 0x085282a0"

run() {   # run <tag> <testbin> <game> <hook> [adressen...]
  local tag="$1" tb="$2" game="$3" hook="$4"; shift 4
  local log="/tmp/nr-$tag.log"
  rm -f "$log"
  env "RAVENGUN_HASP_ELF_GAME=$game" "RAVENGUN_HASP_ELF_LIB=$DATA/hasp/ravengun-hasp.so" \
      "RAVENGUN_HASP_GAME=JurassicPark" "RAVENGUN_HASP_DIR=$DATA/hasp" \
      "RAVENGUN_HASP_VERBOSE=1" "RAVENGUN_HASP_ELF_LOG=$log" \
      "$tb" "$game" "$hook" "$@" >"/tmp/nr-$tag.out" 2>&1
  echo "=== $tag   (exitcode $?)"
  grep -E 'laagste|RESULTAAT|AANGEHAAKT|NIET AANGERAAKT' "/tmp/nr-$tag.out" | sed 's/^/    /' | head -16
  echo "    --- hook-oordeel:"
  grep -E 'dynsym:|hasp-symbolen|niet leesbaar|aangehaakt:|totaal' "$log" 2>/dev/null | head -16 | sed 's/^/    | /'
  echo
}

echo "############ 2. JURASSIC PARK — de game met 29 hasp-namen in de dynsym"
echo "   (32-bits; de hook en de gastheer zijn -m32)"
run jp-oud   "$TMP/test32" "$N/Jurassic Park/game" "$TMP/hook32-oud.so"   $JP_ADR
run jp-nieuw "$TMP/test32" "$N/Jurassic Park/game" "$TMP/hook32-nieuw.so" $JP_ADR

echo "############ 3. HALO — nul hasp-namen in de dynsym (de adres-route doet daar het werk)"
echo "   (de naam-route hoort hier in BEIDE varianten niets te doen)"
run halo-oud   "$TMP/test64" "$N/Halo Fireteam/g7/halo/game" "$TMP/hook64-oud.so"   0xb2d630 0xb2fd63
run halo-nieuw "$TMP/test64" "$N/Halo Fireteam/g7/halo/game" "$TMP/hook64-nieuw.so" 0xb2d630 0xb2fd63

echo "############ 4. TERMINATOR — nul hasp-namen in de dynsym"
run term-oud   "$TMP/test32" "$N/Terminator Salvation /game" "$TMP/hook32-oud.so"   0x08529650
run term-nieuw "$TMP/test32" "$N/Terminator Salvation /game" "$TMP/hook32-nieuw.so" 0x08529650

echo "== done ($TMP)"
