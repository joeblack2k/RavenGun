#!/bin/bash
# Pre-flight en start van RUN 20 als SYSTEMD-UNIT (zo overleeft hij het einde van de SSH-sessie;
# een losse setsid-nohup bleek toch opgeruimd te worden zodra de sessie sloot, en dat heeft de
# eerste poging van 14:34:30 gekost).
echo "=== PRE-FLIGHT $(date '+%F %H:%M:%S') ==="
echo "--- native kant:"; pgrep -af 'play-native|/pm/g7/halo/game' | grep -v pgrep
echo "--- wine kant:";   pgrep -af 'OpenParrotLoader|Shell.exe|Hodzero|amdaemon' | grep -v pgrep
echo "--- gamescope:";   pgrep -af 'gamescope' | grep -v pgrep | head -3
echo "(lege lijsten = vrij)"
if pgrep -af 'play-native|/pm/g7/halo/game' | grep -v pgrep >/dev/null; then echo "*** NATIVE BEZET"; exit 1; fi
if pgrep -af 'OpenParrotLoader|Shell.exe|Hodzero|amdaemon' | grep -v pgrep >/dev/null; then echo "*** WINE BEZET"; exit 1; fi
pgrep -af 'gamescope' | grep -v pgrep >/dev/null && { echo "*** GAMESCOPE DRAAIT NOG"; exit 1; }
echo "--- oude unit opruimen (indien aanwezig):"
systemctl --user reset-failed rg-run20.service 2>/dev/null
systemctl --user stop rg-run20.service 2>/dev/null
echo "--- proef: werkt systemd-run --user?"
systemd-run --user --unit=rg-proef --collect --wait /bin/true && echo "    JA, systemd-run werkt" || echo "    NEE"
echo "=== RUN 20 starten als rg-run20.service ==="
rm -f /home/deck/run20.out
systemd-run --user --unit=rg-run20 --collect --description="Halo run 20 maskerlezing" \
  /home/deck/run20_wrap.sh 2>&1 | head -5
sleep 3
systemctl --user is-active rg-run20.service
echo "--- eerste regels:"
head -12 /home/deck/run20.out 2>/dev/null
