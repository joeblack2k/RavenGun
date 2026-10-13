#!/bin/bash
# Reporting fix, proven without the bench, and with a no-behaviour-change check.
#
#   VOOR = tools/rt-hasphook/hasp_inline_hook.c uit git HEAD (de gecommitte versie)
#   NA   = de werkboom (de rapportage-wijziging)
# Beide worden gebouwd en op DEZELFDE twee adreslijsten losgelaten. Daarna:
#   * dezelfde adressen aangehaakt?  (gedrag mag niet veranderen)
#   * de samenvatting: drie gevallen uit elkaar?
set -u
R=~/projects/ravengun
PR=~/projects/ravengun-patches
N=/home/deck/mnt/spidernas_emulation/Teknoparrot
DATA=~/.local/share/ravengun
TMP=$(mktemp -d)
INC=()
[ -e /usr/include/gnu/stubs-32.h ] || INC=(-I"$R/hasp/src/inc")

TERM_LIST="$PR/patches/Terminator/hasp-haken.ini"
HALO_LIST="$PR/patches/Halo/hasp-haken.ini"
TERM_GAME="$N/Terminator Salvation /game"
HALO_GAME="$N/Halo Fireteam/g7/halo/game"
TERM_ADR="0x083bbe60 0x083baac0 0x083bbe00 0x083bad30 0x083bac80 0x083bbd60 0x083bbce0 0x083babe0 0x083ba8a0 0x083bae80 0x083baab0"
HALO_ADR="0xb2d630 0xb2dd48 0xb2fd63 0xb2fe60 0xb2e1aa 0xb2e391 0xb2f5ac"

echo "############ 0. de twee bronnen"
( cd "$R" && git show HEAD:tools/rt-hasphook/hasp_inline_hook.c > "$TMP/voor.c" )
cp "$R/tools/rt-hasphook/hasp_inline_hook.c" "$TMP/na.c"
for v in voor na; do
  printf '  %-5s %6s bytes  sha256 %s\n' "$v" "$(stat -c%s "$TMP/$v.c")" "$(sha256sum "$TMP/$v.c" | cut -c1-16)"
done
echo "  --- verschillen tussen voor en na:"
diff "$TMP/voor.c" "$TMP/na.c" | head -60 | sed 's/^/    /'

echo
echo "############ 1. bouwen (32 en 64 bits, beide varianten) + het testprogramma"
for v in voor na; do
  gcc -m32 "${INC[@]}" -shared -fPIC -O2 -w -o "$TMP/h32-$v.so" "$TMP/$v.c" -ldl -lpthread || exit 1
  gcc -shared -fPIC -O2 -w -o "$TMP/h64-$v.so" "$TMP/$v.c" -ldl -lpthread || exit 1
done
gcc -m32 "${INC[@]}" -Wall -O2 -o "$TMP/t32" "$PR/patches/Halo/probe/hook-name-route-test.c" -ldl || exit 1
gcc -Wall -O2 -o "$TMP/t64" "$PR/patches/Halo/probe/hook-name-route-test.c" -ldl || exit 1
echo "  ok"

run() {   # run <variant> <game-key> <testbin> <game> <hook> <lib> <list> <adressen...>
  local v="$1" key="$2" tb="$3" game="$4" hook="$5" lib="$6" lijst="$7"; shift 7
  local log="/tmp/rap-$key-$v.log"
  rm -f "$log"
  env "RAVENGUN_HASP_ELF_GAME=$game" "RAVENGUN_HASP_ELF_LIB=$lib" \
      "RAVENGUN_HASP_GAME=$key" "RAVENGUN_HASP_DIR=$DATA/hasp" \
      "RAVENGUN_HASP_VERBOSE=1" "RAVENGUN_HASP_ELF_ADRESLIJST=$lijst" \
      "RAVENGUN_HASP_ELF_LOG=$log" \
      "$tb" "$game" "$hook" "$@" >"/tmp/rap-$key-$v.out" 2>&1
  local rc=$?
  echo "  --- $key / $v  (exitcode $rc)"
  echo "      per adres:"
  grep -E 'AANGEHAAKT|NIET AANGERAAKT|RESULTAAT' "/tmp/rap-$key-$v.out" \
    | sed 's/^ */        /' | head -14
  echo "      samenvatting uit het logboek:"
  grep -E 'niet aangehaakt:|totaal aangehaakt' "$log" 2>/dev/null | sed 's/^ */        | /'
  echo
}

echo "############ 2. TERMINATOR — de lijst met de verwachte weigering (hasp_free)"
echo "   (game: $(stat -c%s "$TERM_GAME") bytes, e_entry volgens de lijst 0x8057240)"
run voor Terminator "$TMP/t32" "$TERM_GAME" "$TMP/h32-voor.so" "$DATA/hasp/ravengun-hasp.so" "$TERM_LIST" $TERM_ADR
run na   Terminator "$TMP/t32" "$TERM_GAME" "$TMP/h32-na.so"   "$DATA/hasp/ravengun-hasp.so" "$TERM_LIST" $TERM_ADR

echo "############ 3. HALO — gevallen 1 en 3 moeten ongewijzigd blijven"
run voor Halo "$TMP/t64" "$HALO_GAME" "$TMP/h64-voor.so" "$DATA/hasp/ravengun-hasp-x64.so" "$HALO_LIST" $HALO_ADR
run na   Halo "$TMP/t64" "$HALO_GAME" "$TMP/h64-na.so"   "$DATA/hasp/ravengun-hasp-x64.so" "$HALO_LIST" $HALO_ADR

echo "############ 4. gedrag ongewijzigd? vergelijk de aangehaakte adressen per game"
for key in Terminator Halo; do
  a=$(grep -E 'AANGEHAAKT|NIET AANGERAAKT' "/tmp/rap-$key-voor.out" | awk '{print $1, $NF}' | sort)
  b=$(grep -E 'AANGEHAAKT|NIET AANGERAAKT' "/tmp/rap-$key-na.out"   | awk '{print $1, $NF}' | sort)
  if [ "$a" = "$b" ]; then
    echo "  $key: IDENTIEK — dezelfde adressen, dezelfde uitkomst ($(echo "$a" | grep -c AANGEHAAKT) aangehaakt)"
  else
    echo "  $key: VERSCHIL!"
    diff <(echo "$a") <(echo "$b") | sed 's/^/    /'
  fi
done
echo "== done ($TMP)"
