#!/bin/bash
# What is REALLY mapped in the running game process (run from a script file so pgrep does not match itself)
echo "now $(date +%H:%M:%S)"
for p in $(pgrep -x KSHG.exe); do
  echo "pid $p comm $(cat /proc/$p/comm) threads $(ls /proc/$p/task | wc -l) exe $(readlink /proc/$p/exe | sed 's#.*/##')"
  for lib in 'views/SilentHill/shaiolib.dll' 'OpenParrotWin32/OpenParrot.dll' 'TeknoParrot/TeknoParrot.dll' 'winegstreamer.so' 'GE-Proton10-34/files/lib/i386-linux-gnu/libgstreamer' 'libgstlibav.so' 'gst32/libvpx' 'KSHG.exe' 'quartz.dll' 'd3d9.dll' 'dsound.dll'; do echo "    maps: $lib -> $(grep -c "$lib" /proc/$p/maps) mappings"; done
done
