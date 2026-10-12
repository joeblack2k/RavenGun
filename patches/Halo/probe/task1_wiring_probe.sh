#!/bin/bash
# Task 1, bench-free. Nothing is started; the NAS is only read.
#   A. which of the three libraries provides what (symbol-level truth)
#   B. halo.so's own inline-hook target list for Halo (the missing piece for our hook)
#   C. dry runs under /bin/ls with the real LD_PRELOAD and the real env
#   D. the emulator selftest against the installed Halo dongle data
set -u
G="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo"
R=~/projects/ravengun
D=~/projects/ravengun-patches/patches/Halo/dropin
DATA=~/.local/share/ravengun
STUB="$DATA/runtime/lib-fmodstub"
TMP=$(mktemp -d)

echo "############ A. who provides what"
echo "--- librg_rt_input.so: does it interpose anything at all?"
nm -D --defined-only "$R/build/librg_rt_input.so" 2>/dev/null | head -20
echo "    (its constructor returns unless /proc/self/exe ends in /game — read from the source)"
echo "--- hasp_* providers among the preload candidates:"
for so in "$G/halo.so" "$R/build/librg_rt_input.so" "$D/libravengun-hasphook-x64.so" "$D/ravengun-hasp-x64.so" "$DATA/hasp/ravengun-hasp-x64.so"; do
  [ -f "$so" ] || { printf '  %-58s MISSING\n' "$so"; continue; }
  printf '  %-58s hasp_* exports: %s\n' "$(basename "$so")" \
    "$(nm -D --defined-only "$so" 2>/dev/null | grep -c ' T hasp_')"
done
echo "--- does our hasphook interpose any symbol (i.e. does LD_PRELOAD order matter for it)?"
nm -D --defined-only "$D/libravengun-hasphook-x64.so" 2>/dev/null | grep ' T ' | while read -r a b c; do echo "    $c"; done | head -20

echo
echo "############ B. halo.so's inline-hook targets for Halo"
SO="$G/halo.so"
echo "--- initialize / update_hook addresses:"
nm -D --defined-only "$SO" | grep -iE ' initialize|update_hook|flh_inline_hook'
echo "--- disassembly of initialize:"
objdump -d --disassemble=initialize -M intel "$SO" 2>/dev/null | grep -nE 'mov +esi|lea +rsi|mov +edx|call' | head -60
echo "--- all immediate arguments passed to flh_inline_hook*:"
objdump -d -M intel "$SO" 2>/dev/null | grep -B6 -E 'call.*<flh_inline_hook' | grep -E 'mov +esi,0x|lea +rsi|mov +edi,0x' | head -40

echo
echo "############ C. dry runs under /bin/ls (safe: rt_input returns for a non-/game exe,"
echo "############    and the hook is put in measure-only mode with RAVENGUN_HASP_ELF_UIT=1)"
COMMON="RAVENGUN_HASP_ELF_GAME=$G/game RAVENGUN_HASP_GAME=Halo"
COMMON="$COMMON RAVENGUN_HASP_DIR=$DATA/hasp RAVENGUN_HASP_ELF_LIB=$DATA/hasp/ravengun-hasp-x64.so"
COMMON="$COMMON RAVENGUN_HASP_VERBOSE=1 RAVENGUN_HASP_ELF_UIT=1"

echo "=== C1: our chain, halo.so NOT in LD_PRELOAD"
rm -f /tmp/hh-c1.log
env $COMMON RAVENGUN_HASP_ELF_LOG=/tmp/hh-c1.log \
  LD_PRELOAD="$R/build/librg_rt_input.so:$D/libravengun-hasphook-x64.so" /bin/ls /dev/null
echo "exitcode $?  --- log:"
cat /tmp/hh-c1.log 2>/dev/null || echo "  (no log)"

echo
echo "=== C2: same, with halo.so FIRST (the old and the new hook in one process)"
rm -f /tmp/hh-c2.log
env $COMMON RAVENGUN_HASP_ELF_LOG=/tmp/hh-c2.log \
  LD_PRELOAD="$G/halo.so:$R/build/librg_rt_input.so:$D/libravengun-hasphook-x64.so" /bin/ls /dev/null
echo "exitcode $?  --- log:"
cat /tmp/hh-c2.log 2>/dev/null || echo "  (no log)"

echo
echo "=== C3: the naive wiring (emulator IN LD_PRELOAD) — shows the dongle loading, and why it is wrong"
rm -f /tmp/hh-c3.log
env $COMMON RAVENGUN_HASP_VERBOSE=1 RAVENGUN_HASP_ELF_LOG=/tmp/hh-c3.log \
  LD_PRELOAD="$D/ravengun-hasp-x64.so:$R/build/librg_rt_input.so:$D/libravengun-hasphook-x64.so" \
  /bin/ls /dev/null 2>&1 | head -6
echo "exitcode $?  --- log:"
cat /tmp/hh-c3.log 2>/dev/null || echo "  (no log)"

echo
echo "=== C4: the same dongle data through the installed emulator, still no game"
env RAVENGUN_HASP_GAME="Halo Fireteam" RAVENGUN_HASP_DIR="$DATA/hasp" RAVENGUN_HASP_VERBOSE=1 \
  LD_PRELOAD="$DATA/hasp/ravengun-hasp-x64.so" /bin/ls /dev/null 2>&1 | head -6

echo
echo "############ D. the emulator's own selftest against the installed Halo data"
cc -std=c99 -Wall -O2 -o "$TMP/hasp-selftest" "$R/hasp/src/selftest.c" "$R/hasp/src/dongle.c" 2>&1 | head -5
echo "build exitcode $?"
"$TMP/hasp-selftest" "$DATA/hasp/Halo Fireteam" 2>&1 | tail -22
echo "selftest exitcode $?"

echo
echo "############ E. the game's DT_NEEDED closure, unchanged (the loader proof)"
LD_LIBRARY_PATH="$STUB" ldd "$G/game" 2>&1 | grep -c 'not found'
echo "  ^ number of 'not found' lines (0 = the kernel loader is enough)"

echo "== done ($TMP)"
