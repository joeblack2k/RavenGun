#!/bin/bash
# usage: n-run.sh <tag> <seconds> [extra play-native args...]   -- one NERF Arcade bench run on the TV, NATIVE route:
# the original Linux ELF Nerf.x86_64 through scripts/play-native.sh (no Wine, no TeknoParrot core, no BepInEx).
# Environment (exported by the caller): SHOTSECS=<s>, N_NOTE="<what this run changes>", N_GAMESCOPE=1 (run inside gamescope).
# The run's own record is <tag>.run: what was declared, the launcher's identity, and (n-watch.sh) every 30 s what is
# mapped in the live game process.
TAG=$1; SECS=$2
O=/tmp/claude-1000/-home-deck/6c50f9d7-7b90-4218-b922-50975534fcdb/scratchpad/nerf-bench
NAS="/home/deck/mnt/spidernas_emulation/Teknoparrot/NERF Arcade"
REC=$O/$TAG.run
START=$(date '+%Y-%m-%d %H:%M:%S')
{ echo "RUN $TAG -- NERF Arcade -- started $START"
  echo "DECLARED: route=native (Nerf.x86_64 via scripts/play-native.sh --game NERF; no Wine, no TeknoParrot core, no BepInEx)  limit=${SECS}s  captures every ${SHOTSECS:-10}s  gamescope=${N_GAMESCOPE:-0}"
  echo "THIS RUN: ${N_NOTE:-(no note)}"
  echo "extra launcher arguments: ${*:3}"
} > $REC
echo "$TAG start $(date +%H:%M:%S)" > $O/$TAG.txt
bash $O/n-watch.sh "$TAG" & WATCH=$!
timeout $((SECS+120)) /home/deck/projects/ravengun/scripts/rg-tv-env.sh -- bash $O/n-inner.sh "$@" > $O/$TAG.outer 2>&1
echo "$TAG outer-rc=$? end $(date +%H:%M:%S)" >> $O/$TAG.txt
wait $WATCH 2>/dev/null
# keep this run's own logs of the game, and take them out of the local write layer so the next run starts from the share's copies
U=/home/deck/.local/share/ravengun/native/NERF/0/upper
for f in LocalData/logs/gamelog.txt LocalData/logs/errorlog.txt .config/unity3d/RawThrills/Nerf/Player.log; do
  [ -e "$U/$f" ] && { head -c 400000 "$U/$f" > "$O/$TAG-$(basename $f)"; echo "$(basename $f): $(wc -c < "$U/$f") bytes, $(wc -l < "$U/$f") lines (first 400 kB kept)" >> $REC; rm -f -- "$U/$f"; }
done
cp "$(ls -t /home/deck/.local/state/ravengun/logs/native-NERF-*.log | head -1)" $O/$TAG-launcher.log 2>/dev/null
{ echo "ENDED $(date '+%Y-%m-%d %H:%M:%S') -- launcher exit: $(tail -1 $O/$TAG.out)"
  echo "left running afterwards: $(pgrep -x Nerf.x86_64 | tr '\n' ' ')(end of list)"
  echo "files in the share's game folder newer than the start of this run: $(find "$NAS" -newermt "$START" 2>/dev/null | wc -l)"
  echo "captures: $(ls $O/$TAG-shots 2>/dev/null | grep -c png), first $(ls $O/$TAG-shots | grep png | head -1), last $(ls $O/$TAG-shots | grep png | tail -1)"
} >> $REC
