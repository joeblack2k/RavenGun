#!/bin/bash
# Run by hand NEXT TO a live run, after the untouched part. usage: win-play.sh <exact comm> <tag> <seconds-untouched> <start|test>
# start: START for player 1 (key 1 held 0.6 s), then aim at five points with a trigger press on each.
# test : TEST (key 0, 0.4 s) and, 20 s later, one coin (key 5).
# Every press is logged with its time and the bridge's shared state during it. The bridge must already run (win-bridge.sh start).
COMM=$1; TAG=$2; WAIT=$3; WHAT=$4
O=/tmp/claude-1000/-home-deck/6c50f9d7-7b90-4218-b922-50975534fcdb/scratchpad/win-bench
export XDG_RUNTIME_DIR=/run/user/1000
L=$O/$TAG-play.txt
until pgrep -x "$COMM" >/dev/null; do sleep 1; done
echo "game process seen $(date +%T); untouched for $WAIT s from now" > $L
sleep "$WAIT"
shm() { echo "  shm $1 $(date +%T): $(xxd -p -c 64 /dev/shm/ravengun_JvsState | cut -c1-56)" >> $L; }
ravengunctl iobridge status >> $L 2>&1
if [ "$WHAT" = start ]; then
  shm "before START"; echo "START down $(date +%T)" >> $L; ravengunctl key 1 down >> $L 2>&1; sleep 0.6; shm "during START"; ravengunctl key 1 up >> $L 2>&1
  sleep 12
  for pt in "0.5 0.5" "0.25 0.4" "0.75 0.4" "0.5 0.75" "0.4 0.55"; do
    ravengunctl test-pattern point $pt >> $L 2>&1; sleep 1.2
    echo "trigger at $pt $(date +%T)" >> $L; ravengunctl output-button trigger down >> $L 2>&1; sleep 0.25; shm "during trigger"; ravengunctl output-button trigger up >> $L 2>&1; sleep 1.5
    ravengunctl output-button trigger tap >> $L 2>&1; sleep 1.5
  done
else
  shm "before TEST"; echo "TEST down $(date +%T)" >> $L; ravengunctl key 0 down >> $L 2>&1; sleep 0.4; shm "during TEST"; ravengunctl key 0 up >> $L 2>&1
fi
echo "presses done $(date +%T)" >> $L
