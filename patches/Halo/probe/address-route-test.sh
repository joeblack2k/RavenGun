#!/bin/bash
# Bench-free proof of the address route. Nothing is started; the NAS is only read.
#   A. both widths still build, and the name-route self-test still passes (regression)
#   B. the address route hooks seven functions on a real mapped image where the name route found 0
#   C. the loud refusals: a bad prologue, and a wrong build
#   D. the report-only dry run (RAVENGUN_HASP_ELF_UIT=1) with the real address list
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
DATA=~/.local/share/ravengun
G="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo"
TMP=$(mktemp -d)

echo "############ A1. both widths build, la_version present (scripts/build-hasphook.sh --no-place)"
bash "$R/scripts/build-hasphook.sh" --no-place 2>&1 | tail -8

echo
echo "############ A2. the existing 32-bit name-route self-test (must still pass)"
bash "$R/scripts/build-hasphook.sh" --no-place --selftest 2>&1 | sed -n '/zelftest/,$p' | tail -14

echo
echo "############ B. the address route on a real mapped image"
gcc -shared -fPIC -O2 -Wall -Wextra -Wno-format-truncation \
    -o "$TMP/libravengun-hasphook-x64.so" "$R/tools/rt-hasphook/hasp_inline_hook.c" -ldl -lpthread
echo "hook build exitcode $?"
gcc -Wall -O2 -o "$TMP/hook-address-route-test" "$P/probe/hook-address-route-test.c" -ldl
echo "test build exitcode $?"

E=( "RAVENGUN_HASP_ELF_GAME=$G/game"
    "RAVENGUN_HASP_ELF_LIB=$DATA/hasp/ravengun-hasp-x64.so"
    "RAVENGUN_HASP_GAME=Halo Fireteam"
    "RAVENGUN_HASP_DIR=$DATA/hasp"
    "RAVENGUN_HASP_VERBOSE=1" )

echo
echo "--- B1. POSITIVE: the real list"
rm -f /tmp/hook-adres.log
env "${E[@]}" "RAVENGUN_HASP_ELF_ADRESLIJST=$P/hasp-haken.ini" \
    "RAVENGUN_HASP_ELF_LOG=/tmp/hook-adres.log" "$TMP/hook-address-route-test" \
    "$G/game" "$TMP/libravengun-hasphook-x64.so"
echo "exitcode $?"
echo "--- the hook's own log:"
sed 's/^/    | /' /tmp/hook-adres.log

echo
echo "--- B2. NEGATIVE: one address with no prologue (a data address), rest valid"
sed 's/^hasp_login .*/hasp_login           = 0x400000/' "$P/hasp-haken.ini" > "$TMP/fout1.ini"
rm -f /tmp/hook-fout1.log
env "${E[@]}" "RAVENGUN_HASP_ELF_ADRESLIJST=$TMP/fout1.ini" \
    "RAVENGUN_HASP_ELF_LOG=/tmp/hook-fout1.log" "$TMP/hook-address-route-test" \
    "$G/game" "$TMP/libravengun-hasp-x64.so" | tail -12
echo "exitcode $?"
echo "--- the refusal lines:"
grep -E 'AFGEWEZEN|AFGEKEURD|totaal aangehaakt' /tmp/hook-fout1.log | sed 's/^/    | /'

echo
echo "--- B3. NEGATIVE: a wrong build (grootte klopt niet) -> the WHOLE list refused"
sed 's/^grootte .*/grootte = 1234/' "$P/hasp-haken.ini" > "$TMP/fout2.ini"
rm -f /tmp/hook-fout2.log
env "${E[@]}" "RAVENGUN_HASP_ELF_ADRESLIJST=$TMP/fout2.ini" \
    "RAVENGUN_HASP_ELF_LOG=/tmp/hook-fout2.log" "$TMP/hook-address-route-test" \
    "$G/game" "$TMP/libravengun-hasphook-x64.so" | tail -12
echo "exitcode $?"
echo "--- the refusal lines:"
grep -E 'AFGEKEURD|AFGEWEZEN|totaal aangehaakt' /tmp/hook-fout2.log | head -4 | sed 's/^/    | /'

echo
echo "############ D. report-only dry run under /bin/ls (no patching), with the real list"
rm -f /tmp/hook-uit.log
env "${E[@]}" "RAVENGUN_HASP_ELF_UIT=1" "RAVENGUN_HASP_ELF_ADRESLIJST=$P/hasp-haken.ini" \
    "RAVENGUN_HASP_ELF_LOG=/tmp/hook-uit.log" \
    "LD_PRELOAD=$TMP/libravengun-hasphook-x64.so" /bin/ls /dev/null >/dev/null 2>&1
echo "exitcode $?"
sed 's/^/    | /' /tmp/hook-uit.log

echo
echo "== done ($TMP)"
