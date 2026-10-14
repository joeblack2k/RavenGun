#!/bin/bash
TAG=$1; SECS=$2; GAMEBIN=$3; shift 3
D=/tmp/claude-1000/-home-deck/6c50f9d7-7b90-4218-b922-50975534fcdb/scratchpad/jp; R=$D/runs/$TAG; B0=/tmp/claude-1000/-home-deck/6c50f9d7-7b90-4218-b922-50975534fcdb/scratchpad/jp-bench
SHARE="/home/deck/mnt/spidernas_emulation/Teknoparrot/Jurassic Park"
T0=$(date +%s)
( i=0; while [ ! -e $R/shots/stop ]; do i=$((i+1)); spectacle -b -n -f -o $R/shots/s-$(printf %03d $i)-$(date +%H%M%S).png >/dev/null 2>&1; sleep 10; done ) &
SH=$!
if [[ -f $R/schedule.txt ]]; then
  ( while read -r t rest; do [[ -z "$t" || "$t" == \#* ]] && continue
      while (( $(date +%s) - T0 < t )); do sleep 0.2; done
      echo "$rest" >> $R/meter/inject; echo "$(date +%H:%M:%S) t=$t inject: $rest" >> $R/driver.log
      sleep 1.5; spectacle -b -n -f -o $R/shots/inj-t$t-$(echo "$rest" | tr ' ' '_')-$(date +%H%M%S).png >/dev/null 2>&1
    done < $R/schedule.txt ) &
  DR=$!
fi
B=(--bind-ro "$B0/jp-slots.tsv" /rg/jp-slots.tsv --bind-ro "$B0/udev-rules.d" /etc/udev/rules.d
   --bind-ro "$SHARE/programs_dec" /pm/g5/dino/data/programs_enc --bind-ro "$GAMEBIN" /pm/game --bind-rw "$R/meter" /rg/meter)
E=(--env RG_ELFLOADER_VERBOSE=1 --env RG_ELF_SLOT_TABLE=/rg/jp-slots.tsv
   --env LD_PRELOAD=/pm/libravengun-hasphook.so:/pm/librg-threadprio.so:/rg/meter/jp_rio_meter.so --env RG_THREADPRIO_LOG=/rg/meter/threadprio.log
   --env RG_JP_METER_DIR=/rg/meter --env SDL12COMPAT_OPENGL_SCALING=0
   --env RAVENGUN_HASP_DIR=ravengun-hasp --env RAVENGUN_HASP_GAME=JurassicPark --env RAVENGUN_HASP_ELF_LIB=./ravengun-hasp.so --env RAVENGUN_HASP_ELF_LOG=/rg/meter/hasphook.log)
cd /home/deck/projects/ravengun
scripts/play-native.sh --game JurassicPark --loader --gamescope --seconds $SECS "${E[@]}" "${B[@]}" "$@" > $R/play.out 2>&1
echo "rc=$?" >> $R/play.out
touch $R/shots/stop; sleep 1; kill $SH ${DR:-} 2>/dev/null
