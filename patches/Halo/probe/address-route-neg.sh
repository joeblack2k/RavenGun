#!/bin/bash
# Re-run the two negative cases with correct output capture and real exit codes.
# Also place the rebuilt hook in Halo's drop-in dir (and only there).
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
DATA=~/.local/share/ravengun
G="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo"
TMP=$(mktemp -d)

gcc -shared -fPIC -O2 -Wall -Wextra -Wno-format-truncation \
    -o "$TMP/libravengun-hasphook-x64.so" "$R/tools/rt-hasphook/hasp_inline_hook.c" -ldl -lpthread || exit 1
gcc -Wall -O2 -o "$TMP/test" "$P/probe/hook-address-route-test.c" -ldl || exit 1

E=( "RAVENGUN_HASP_ELF_GAME=$G/game"
    "RAVENGUN_HASP_ELF_LIB=$DATA/hasp/ravengun-hasp-x64.so"
    "RAVENGUN_HASP_GAME=Halo Fireteam"
    "RAVENGUN_HASP_DIR=$DATA/hasp"
    "RAVENGUN_HASP_VERBOSE=1" )

run() {   # run <tag> <lijst>
  local tag="$1" lijst="$2" log="/tmp/hook-$1.log"
  rm -f "$log"
  env "${E[@]}" "RAVENGUN_HASP_ELF_ADRESLIJST=$lijst" "RAVENGUN_HASP_ELF_LOG=$log" \
      "$TMP/test" "$G/game" "$TMP/libravengun-hasphook-x64.so" >"/tmp/uit-$tag.txt" 2>&1
  echo "=== $tag  (lijst: $lijst)"
  echo "    exitcode $?"
  grep -E 'aangehaakt$|AANGEHAAKT|NIET AANGERAAKT|GESLAAGD|MISLUKT' "/tmp/uit-$tag.txt" | tail -12 | sed 's/^/    /'
  echo "    --- hook-oordeel:"
  grep -E 'AFGEWEZEN|AFGEKEURD|GELDIG|totaal aangehaakt' "$log" | sed 's/^/    | /'
  echo
}

sed 's/^hasp_login .*/hasp_login           = 0x400000/' "$P/hasp-haken.ini" > "$TMP/fout1.ini"
sed 's/^grootte .*/grootte = 1234/' "$P/hasp-haken.ini" > "$TMP/fout2.ini"
sed 's/^hasp_read .*/hasp_read            = 0xb2d800/' "$P/hasp-haken.ini" > "$TMP/fout3.ini"

echo "############ de drie negatieve gevallen"
run fout1 "$TMP/fout1.ini"     # adres zonder prologue (het ELF-hoofd, 0x400000)
run fout2 "$TMP/fout2.ini"     # verkeerde build: grootte klopt niet -> hele lijst afgekeurd
run fout3 "$TMP/fout3.ini"     # midden in een functie: bytes zijn geen prologue

echo "############ het positieve geval, voor de volledigheid"
run goed "$P/hasp-haken.ini" | head -20

echo "############ de herbouwde hook in de drop-in van Halo zetten (en alleen daar)"
bash "$R/scripts/build-hasphook.sh" --game Halo 2>&1 | tail -6
echo "--- en wat er in de andere drie games staat (nu achter op de bron):"
bash "$R/scripts/build-hasphook.sh" --check 2>&1 | tail -10
echo "== done ($TMP)"
