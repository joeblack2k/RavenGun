#!/bin/bash
P=/home/deck/projects/ravengun-patches/patches/Halo/probe
echo "=== PRE-FLIGHT $(date '+%F %H:%M:%S') ==="
pgrep -af 'play-native|/pm/g7/halo/game' | grep -v pgrep
pgrep -af 'OpenParrotLoader|Shell.exe|Hodzero|amdaemon' | grep -v pgrep
pgrep -af gamescope | grep -v pgrep | head -3
pgrep -af 'play-native|/pm/g7/halo/game' | grep -v pgrep >/dev/null && { echo BEZET; exit 1; }
pgrep -af 'OpenParrotLoader|Shell.exe|Hodzero|amdaemon' | grep -v pgrep >/dev/null && { echo BEZET; exit 1; }
pgrep -af gamescope | grep -v pgrep >/dev/null && { echo "gamescope draait nog"; exit 1; }
bash -n "$P/run22_uitvoeren.sh" && echo "bash OK"
python3 -m py_compile "$P/rk.py" "$P/zoek.py" && echo "python OK"
printf '%s\n' '#!/bin/bash' 'exec bash '"$P"'/run22_uitvoeren.sh > /home/deck/run22.out 2>&1' > /home/deck/run22_wrap.sh
chmod +x /home/deck/run22_wrap.sh
systemctl --user reset-failed rg-run22.service 2>/dev/null; systemctl --user stop rg-run22.service 2>/dev/null
rm -f /home/deck/run22.out
systemd-run --user --unit=rg-run22 --collect --description="Halo run 22 analoogvak" /home/deck/run22_wrap.sh 2>&1 | head -3
sleep 4
systemctl --user is-active rg-run22.service
head -6 /home/deck/run22.out
