#!/bin/bash
# Pre-flight en start van RUN 21 als systemd-unit.
P=/home/deck/projects/ravengun-patches/patches/Halo/probe
echo "=== PRE-FLIGHT $(date '+%F %H:%M:%S') ==="
echo "--- native:"; pgrep -af 'play-native|/pm/g7/halo/game' | grep -v pgrep
echo "--- wine:";   pgrep -af 'OpenParrotLoader|Shell.exe|Hodzero|amdaemon' | grep -v pgrep
echo "--- gamescope:"; pgrep -af 'gamescope' | grep -v pgrep | head -3
echo "(leeg = vrij)"
pgrep -af 'play-native|/pm/g7/halo/game' | grep -v pgrep >/dev/null && exit 1
pgrep -af 'OpenParrotLoader|Shell.exe|Hodzero|amdaemon' | grep -v pgrep >/dev/null && exit 1
pgrep -af gamescope | grep -v pgrep >/dev/null && { echo "*** gamescope draait nog"; exit 1; }
echo "--- syntaxcontrole:"
bash -n "$P/run21_uitvoeren.sh" && echo "    bash OK"
python3 -m py_compile "$P/rk.py" && echo "    rk.py OK"
chmod +x /home/deck/run21_wrap.sh
systemctl --user reset-failed rg-run21.service 2>/dev/null; systemctl --user stop rg-run21.service 2>/dev/null
rm -f /home/deck/run21.out
systemd-run --user --unit=rg-run21 --collect --description="Halo run 21 aim-brug" /home/deck/run21_wrap.sh 2>&1 | head -3
sleep 4
systemctl --user is-active rg-run21.service
head -8 /home/deck/run21.out 2>/dev/null
