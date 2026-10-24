#!/bin/bash
# Start de statische IO-brug-analyse losgekoppeld van deze SSH-sessie (setsid).
P=/home/deck/projects/ravengun-patches/patches/Halo/probe
chmod +x "$P/io_brug_static.sh"
rm -f /home/deck/io-brug-static.out
setsid nohup bash "$P/io_brug_static.sh" > /home/deck/io-brug-static.out 2>&1 < /dev/null &
sleep 1
echo "gestart; pgrep zegt:"
pgrep -af io_brug_static | grep -v pgrep
sleep 3
echo "eerste regels:"
head -5 /home/deck/io-brug-static.out
