#!/bin/bash
# Task 1, dry runs — with correct quoting (the game path contains a space).
# /bin/ls only; nothing is started; the NAS is only read.
#   librg_rt_input.so  -> constructor returns unless /proc/self/exe ends in /game (source-verified)
#   our hasphook       -> RAVENGUN_HASP_ELF_UIT=1 makes it measure only, patch nothing
set -u
G="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo"
R=~/projects/ravengun
D=~/projects/ravengun-patches/patches/Halo/dropin
DATA=~/.local/share/ravengun

E=( "RAVENGUN_HASP_ELF_GAME=$G/game"
    "RAVENGUN_HASP_GAME=Halo"
    "RAVENGUN_HASP_DIR=$DATA/hasp"
    "RAVENGUN_HASP_ELF_LIB=$DATA/hasp/ravengun-hasp-x64.so"
    "RAVENGUN_HASP_VERBOSE=1"
    "RAVENGUN_HASP_ELF_UIT=1" )

run() {
  local tag="$1" pre="$2" log="/tmp/hh-$1.log"
  rm -f "$log"
  echo "=== $tag"
  echo "    LD_PRELOAD=$pre"
  env "${E[@]}" "RAVENGUN_HASP_ELF_LOG=$log" "LD_PRELOAD=$pre" /bin/ls /dev/null >/dev/null 2>&1
  echo "    exitcode $?"
  if [ -s "$log" ]; then sed 's/^/    | /' "$log"; else echo "    (no log written)"; fi
  echo
}

echo "############ the three chains, under /bin/ls, measure-only"
run C1 "$R/build/librg_rt_input.so:$D/libravengun-hasphook-x64.so"
run C2 "$G/halo.so:$R/build/librg_rt_input.so:$D/libravengun-hasphook-x64.so"
run C3 "$D/ravengun-hasp-x64.so:$R/build/librg_rt_input.so:$D/libravengun-hasphook-x64.so"

echo "############ the same hook, but pointed at the 32-bit sister game (does it find names there?)"
JP="/home/deck/mnt/spidernas_emulation/Teknoparrot/Jurassic Park"
if [ -f "$JP/game" ]; then
  E2=( "RAVENGUN_HASP_ELF_GAME=$JP/game" "RAVENGUN_HASP_GAME=JurassicPark"
       "RAVENGUN_HASP_DIR=$DATA/hasp" "RAVENGUN_HASP_VERBOSE=1" "RAVENGUN_HASP_ELF_UIT=1" )
  rm -f /tmp/hh-jp.log
  env "${E2[@]}" "RAVENGUN_HASP_ELF_LOG=/tmp/hh-jp.log" \
    "LD_PRELOAD=$D/libravengun-hasphook-x64.so" /bin/ls /dev/null >/dev/null 2>&1
  echo "  (32-bit game under the 64-bit hook — expected to refuse)"
  sed 's/^/    | /' /tmp/hh-jp.log 2>/dev/null || echo "    (no log)"
else
  echo "  Jurassic Park not found"
fi

echo
echo "############ what the 7 addresses halo.so patches look like in the game"
python3 - "$G/game" <<'PY'
import sys, struct
d = open(sys.argv[1], 'rb').read()
addrs = [0xB2E1AA, 0xB2D630, 0xB2DD48, 0xB2FD63, 0xB2FE60, 0xB2E391, 0xB2F5AC]
print("  (vaddr -> file offset -> first 16 bytes; segment 1 maps at off = vaddr-0x400000)")
for a in sorted(addrs):
    o = a - 0x400000
    if 0 <= o < len(d):
        print("   0x%08x  file 0x%08x  %s" % (a, o, " ".join("%02x" % b for b in d[o:o+16])))
    else:
        print("   0x%08x  NOT IN FILE" % a)
PY
echo "== done"
