#!/bin/bash
# Read-only recon on the Deck for Halo: Fireteam Raven.
# Nothing is written to the NAS; no game is started.
set -u
NAS=/home/deck/mnt/spidernas_emulation/Teknoparrot
G="$NAS/Halo Fireteam/g7/halo"
R=~/projects/ravengun

echo "############ 1. NAS game folder: identity of the binaries"
for f in game game_original Patch halo.so; do
  if [ -f "$G/$f" ]; then
    printf '%-16s %12s  %s\n' "$f" "$(stat -c%s "$G/$f")" "$(sha256sum "$G/$f" | cut -c1-16)"
  fi
done
for f in utils/rtinit utils/rtinit_original utils/rtinit.so; do
  [ -f "$G/$f" ] && printf '%-16s %12s  %s\n' "$f" "$(stat -c%s "$G/$f")" "$(sha256sum "$G/$f" | cut -c1-16)"
done
echo "--- dongle-looking files in g7/halo:"
ls -l "$G" | grep -Ev '^d|total' | awk '{print $5, $9}'
echo "--- 112-byte files in the whole game folder (max 40):"
find "$NAS/Halo Fireteam" -maxdepth 4 -type f -size -200c 2>/dev/null | head -40
echo "--- firmware / irgun / guninfo:"
ls -l "$NAS/Halo Fireteam/g7/halo/data" 2>/dev/null | head
find "$NAS/Halo Fireteam" -maxdepth 4 -iname '*irgun*' -o -maxdepth 4 -iname '*prom*' -o -maxdepth 4 -iname '*guninfo*' 2>/dev/null | head -20

echo
echo "############ 2. which DT_NEEDED libraries actually exist on this machine"
for l in libGL.so.1 libGLU.so.1 libpthread.so.0 libdl.so.2 libudev.so.1 \
         liblapack.so.3 libiw.so.30 libfmod.so.10 libfmodstudio.so.10 \
         libstdc++.so.6 libm.so.6 libgcc_s.so.1 libc.so.6; do
  p=$(ldconfig -p | awk -v L="$l" '$1==L {print $NF; exit}')
  printf '  %-22s %s\n' "$l" "${p:-*** NOT IN LDCONFIG ***}"
done

echo
echo "############ 3. the FMOD stub and the other runtime helpers"
ls -l ~/.local/share/ravengun/runtime/lib-fmodstub/ 2>/dev/null
for f in ~/.local/share/ravengun/runtime/lib-fmodstub/*; do
  [ -f "$f" ] && printf '  %-34s %10s  %s\n' "$(basename "$f")" "$(stat -c%s "$f")" "$(file -b "$f")"
done
echo "--- exported FMOD names from the built stub:"
for so in ~/.local/share/ravengun/runtime/lib-fmodstub/libfmod.so.10 ~/.local/share/ravengun/runtime/lib-fmodstub/libfmodstudio.so.10; do
  [ -f "$so" ] || continue
  echo "  == $so : $(nm -D --defined-only "$so" 2>/dev/null | wc -l) defined names"
done
echo "--- symbols.txt in the repo: $(wc -l < $R/tools/fmod-stub/symbols.txt) names"

echo
echo "############ 4. rt-input-shim / halo.so facts recorded in the source"
grep -nE '0x[0-9a-f]{5,}|define |RG_RT|bit|START|TEST' "$R/tools/rt-input-shim/rg_rt_input.c" | head -60

echo
echo "############ 5. what the project already has for the dongle"
ls -l ~/projects/ravengun-patches/patches/Halo/dropin 2>/dev/null
find ~/projects/ravengun-patches/patches/Halo/dropin -type f 2>/dev/null | head -20
ls -l $R/dongles 2>/dev/null | head -20
ls -l $R/dongles/halo-fireteam 2>/dev/null
echo "--- hasp/ dir in the repo:"
ls -l $R/hasp 2>/dev/null | head -20

echo
echo "############ 6. is anything already running? (do not start a game)"
pgrep -a -f 'game|halo|BudgieLoader|play-native|wine' | head -20 || echo "  nothing matching"

echo
echo "############ 7. recent Halo-native logs and views"
ls -lt ~/.local/state/ravengun/logs 2>/dev/null | head -12
ls -l ~/.local/share/ravengun/native 2>/dev/null | head
echo "== done"
