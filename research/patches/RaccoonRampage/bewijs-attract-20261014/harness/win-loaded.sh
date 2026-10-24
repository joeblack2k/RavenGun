#!/bin/bash
# What is REALLY mapped in the running game process. usage: win-loaded.sh <exact comm of the game process>
echo "now $(date +%H:%M:%S)"
for p in $(pgrep -x "$1"); do
  echo "pid $p comm $(cat /proc/$p/comm) threads $(ls /proc/$p/task | wc -l)"
  for lib in 'OpenParrotx64/OpenParrot64.dll' 'TeknoParrot/TeknoParrot64.dll' 'RSGame-Win64-Shipping.exe' 'UsbPluginsDll.dll' 'd3d11.dll' 'dxgi.dll' 'winegstreamer' 'gst-extra/plugins/libgstlibav.so' 'libgstreamer-1.0' 'mfplat.dll' 'mf.dll'; do echo "    maps: $lib -> $(grep -c "$lib" /proc/$p/maps) mappings"; done
  echo "    d3d11 from: $(grep -m1 'd3d11.dll' /proc/$p/maps | awk '{print $6}' | sed "s#$HOME/##")"
done
