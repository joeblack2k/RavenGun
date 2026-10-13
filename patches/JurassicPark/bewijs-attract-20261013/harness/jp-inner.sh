#!/bin/bash
# runs INSIDE rg-tv-env: capture loop + the product launcher. usage: jp-inner.sh <tag> <seconds> [extra play-native args]
TAG=$1; SECS=$2; shift 2; O=/tmp/claude-1000/-home-deck/6c50f9d7-7b90-4218-b922-50975534fcdb/scratchpad/jp-bench
mkdir -p $O/$TAG-shots
( i=0; while [ ! -e $O/$TAG-shots/stop ]; do i=$((i+1)); spectacle -b -n -f -o $O/$TAG-shots/shot-$(printf %03d $i)-$(date +%H%M%S).png >/dev/null 2>&1; sleep ${SHOTSECS:-10}; done ) &
SH=$!
B=(--bind-ro "$O/jp-slots.tsv" /rg/jp-slots.tsv --bind-ro "$O/udev-rules.d" /etc/udev/rules.d)
E=(--env RG_ELFLOADER_VERBOSE=1 --env RG_ELF_SLOT_TABLE=/rg/jp-slots.tsv --env LD_PRELOAD=/pm/libravengun-hasphook.so:/pm/librg-threadprio.so --env RG_THREADPRIO_LOG=/pm/threadprio.log
   --env RAVENGUN_HASP_DIR=ravengun-hasp --env RAVENGUN_HASP_GAME=JurassicPark --env RAVENGUN_HASP_ELF_LIB=./ravengun-hasp.so --env RAVENGUN_HASP_ELF_LOG=hasphook.log --env RAVENGUN_HASP_VERBOSE=1)
cd /home/deck/projects/ravengun
scripts/play-native.sh --game JurassicPark --loader --gamescope --seconds $SECS "${E[@]}" "${B[@]}" "$@" > $O/$TAG.out 2>&1
echo "rc=$?" >> $O/$TAG.out
touch $O/$TAG-shots/stop; sleep 1; kill $SH 2>/dev/null
