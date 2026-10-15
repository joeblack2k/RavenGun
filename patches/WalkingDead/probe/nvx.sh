#!/bin/sh
# nvx.sh <dir> <version>... : download NVIDIA-Linux-x86-<v>.run (us.download.nvidia.com) and unpack ONLY its libGL*
# files into <dir>/x<v>/ by reading the makeself header's skip= line and untarring the payload. The .run is never run.
D=$1; shift
for v in "$@"; do
  [ -d "$D/x$v" ] && { echo "have $v"; continue; }
  f="$D/dl/NVIDIA-Linux-x86-$v.run"; mkdir -p "$D/dl"
  [ -s "$f" ] || curl -sf -m 600 -o "$f" "https://us.download.nvidia.com/XFree86/Linux-x86/$v/NVIDIA-Linux-x86-$v.run" || { echo "FAIL $v"; rm -f "$f"; continue; }
  s=$(head -c 20000 "$f" | grep -a '^skip=' | head -1 | tr -dc 0-9)
  mkdir -p "$D/x$v"
  m=$(tail -n +$s "$f" | head -c 3 | od -An -tx1 | tr -d ' ')
  case $m in fd377a) z=-J;; 425a68) z=-j;; 1f8b*) z=-z;; *) z=;; esac
  tail -n +$s "$f" | tar x $z -C "$D/x$v" --wildcards '*libGL.so*' '*libGLX*.so*' '*libGLdispatch.so*' 2>/dev/null
  n=$(find "$D/x$v" -name 'libGL*' -type f | wc -l); echo "ok $v ($n files)"; rm -f "$f"
done
