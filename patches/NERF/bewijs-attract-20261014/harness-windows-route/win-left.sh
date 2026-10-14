#!/bin/bash
# what is still running after a run (must be nothing of the game)
echo "now $(date +%H:%M:%S)"
ps -eo pid,etimes,comm,args | grep -i -E 'RSGame|OpenParrot|wineserver|rg-jvsbridge|launch-direct|winedevice|explorer.exe' | grep -v -E 'grep|win-left' | cut -c1-200
echo "(end of list)"
