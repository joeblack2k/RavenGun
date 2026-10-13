#!/bin/bash
# CORRECTIE OP RUN 22: de magische getallen zijn 0x0BAD001B..0x0BAD001F (printf %04X van het decimale
# vaknummer), niet 0x0BAD0027..2B. De zoektocht van run 22 zocht dus naar getallen die nooit geschreven
# zijn -- dat is een FOUT IN MIJN PROEF, geen meting. Deze zoektocht gebruikt de ECHTE waarden.
# Positieve controle: ze moeten op de schrijfplek gevonden worden (0x1816598, 0x18165e8, ...).
P=/home/deck/projects/ravengun-patches/patches/Halo/probe
PID=$(python3 -c "
import os
for p in os.listdir('/proc'):
    if p.isdigit():
        try: e=os.readlink('/proc/%s/exe'%p)
        except OSError: continue
        if e.endswith('halo/game'): print(p); break
")
echo "pid=$PID  $(date '+%T')"
if [ -z "$PID" ]; then echo "GEEN GAME MEER -- deze reddingspoging is te laat"; exit 1; fi
echo "--- staat de magie nog op de schrijfplek? (positieve controle vooraf)"
python3 "$P/rk.py" "$PID" lees 0x1816590 0x50
echo "--- de zoektocht met de ECHTE waarden 0x0BAD001B..1F"
python3 "$P/zoek.py" "$PID" 0x0BAD001B 0x0BAD001C 0x0BAD001D 0x0BAD001E 0x0BAD001F 0x0BADFFFF
