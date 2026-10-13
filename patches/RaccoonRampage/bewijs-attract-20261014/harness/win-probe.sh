#!/bin/bash
# Run by hand NEXT TO a live run. usage: win-probe.sh <exact comm> <tag> [profile]
# 1. full-path load proof of the game process, 45 s after it appears;
# 2. two coin presses (key 5) through the daemon's tp-shm bridge, with the shared state dumped before/during/after.
# The bridge must be started and the output armed BEFORE the run (win-bridge.sh start), as scripts/play-halo.sh does.
COMM=$1; TAG=$2; PROF=${3:-RaccoonRampage}
O=/tmp/claude-1000/-home-deck/6c50f9d7-7b90-4218-b922-50975534fcdb/scratchpad/win-bench
export XDG_RUNTIME_DIR=/run/user/1000
L=$O/$TAG-probe.txt
until pgrep -x "$COMM" >/dev/null; do sleep 1; done
echo "game process seen $(date +%T)" > $L
sleep 45
bash $O/win-paths.sh "$COMM" > $O/$TAG-paths.txt
shm() { echo "  shm $1 $(date +%T): $(xxd -p -c 64 /dev/shm/ravengun_JvsState)" >> $L; }
ravengunctl iobridge status >> $L 2>&1
sleep 5
for n in 1 2; do
  shm "before coin $n"
  echo "coin $n down $(date +%T)" >> $L; ravengunctl key 5 down >> $L 2>&1; sleep 0.4
  shm "during coin $n"
  ravengunctl key 5 up >> $L 2>&1; sleep 0.4
  shm "after coin $n"
  sleep 14
done
# 3. TEST (key 0): opens the game's own menu from the attract loop -- an input that needs no bookkeeping write
shm "before TEST"
echo "TEST down $(date +%T)" >> $L; ravengunctl key 0 down >> $L 2>&1; sleep 0.4
shm "during TEST"
ravengunctl key 0 up >> $L 2>&1; sleep 0.4
shm "after TEST"
sleep 20
ravengunctl output >> $L 2>&1
echo "probe done $(date +%T)" >> $L
