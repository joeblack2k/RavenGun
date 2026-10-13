#!/bin/bash
# anything of a game run still alive? (from a script file, so pgrep does not match this command)
for n in KSHG.exe OpenParrotLoader.exe wineserver gamescope-wl rg-jvsbridge.exe winedevice.exe; do c=$(pgrep -c -x "$n" 2>/dev/null); [ "${c:-0}" -gt 0 ] && echo "LEFT: $n x$c"; done; pgrep -af 'KSHG|OpenParrotLoader|launch-direct.py' | grep -v sh-left | cut -c1-120; echo "left-check done"
