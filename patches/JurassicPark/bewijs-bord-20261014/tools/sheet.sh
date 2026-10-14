#!/bin/bash
# sheet.sh OUT.jpg COLS img1 img2 ...   -- contact sheet via ffmpeg (each tile 640x360, filename label)
OUT=$1; COLS=$2; shift 2
n=$#; rows=$(( (n + COLS - 1) / COLS ))
args=(); f=""; i=0
for img in "$@"; do args+=(-i "$img"); lab=$(basename "$img" .png | sed 's/:/-/g');
  f+="[$i:v]scale=640:360,drawtext=text='$lab':x=6:y=6:fontsize=18:fontcolor=yellow:box=1:boxcolor=black@0.6[v$i];"; i=$((i+1)); done
while (( i < rows*COLS )); do args+=(-f lavfi -i "color=black:s=640x360:d=1"); f+="[$i:v]null[v$i];"; i=$((i+1)); done
lay=""; for ((k=0;k<rows*COLS;k++)); do x=$(( (k % COLS) * 640 )); y=$(( (k / COLS) * 360 )); lay+="${x}_${y}|"; done
ins=""; for ((k=0;k<rows*COLS;k++)); do ins+="[v$k]"; done
ffmpeg -loglevel error -y "${args[@]}" -filter_complex "${f}${ins}xstack=inputs=$((rows*COLS)):layout=${lay%|}" -frames:v 1 -q:v 4 "$OUT"
