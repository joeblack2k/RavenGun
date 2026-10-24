#!/bin/bash
# FULL paths of what is mapped in the running game process (the short pattern in win-loaded.sh also matches
# TeknoParrot's own OpenParrotx64 folder). usage: win-paths.sh <exact comm>
echo "now $(date +%H:%M:%S)"
for p in $(pgrep -x "$1"); do
  echo "pid $p comm $(cat /proc/$p/comm) threads $(ls /proc/$p/task | wc -l) started $(ps -o lstart= -p $p)"
  awk '{ $1=$2=$3=$4=$5=""; sub(/^ +/,""); print }' /proc/$p/maps | grep -i -E 'parrot|UsbPlugins|RSGame-Win64|d3d11|dxgi|gstlibav|winegstreamer|mfplat|mfreadwrite|/mf\.dll|jvs' | sort | uniq -c
done
