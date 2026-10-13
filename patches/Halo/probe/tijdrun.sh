#!/bin/bash
# De goedkope tijdrun: HEADLESS, direct na een tv-run (dus met warme bestandscache).
# Doel: het verschil van 11 s (run 2, headless, koud) tegen 1 s (run 3/4/5, tv) verklaren.
# Zelfde opzet als run 2, alleen korter: 60 s, geen pulsen (die zijn voor deze vraag niet nodig).
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
D=~/.local/share/ravengun
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
TAPLOG="$D/tap/usb-tap.log"
OUT=$(mktemp -d /tmp/tijd-XXXX)
REGELS_VOOR=$(wc -l < "$G" 2>/dev/null)

echo "############ 0. vooraf"
date '+  starttijd: %H:%M:%S'
printf '  gamelog had %s regels\n' "$REGELS_VOOR"
pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game' | grep -v pgrep | sed 's/^/  draait al: /'
echo "  (leeg = vrij)"
rm -f "$TAPLOG"

echo
echo "############ 1. de run (HEADLESS, 60 s, direct na de tv-run)"
cd "$R"
( timeout 180 scripts/play-native.sh --game Halo --headless --mute --seconds 60 \
    --bind-ro "$P/probe/libusb-tap.so" /rg/lib/libusb-tap.so \
    --bind-rw "$D/tap" /rg/tap \
    --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
    --env RG_USB_LOG=/rg/tap/usb-tap.log --env RG_USB_RAW=1 --env RG_USB_MAX=50000 \
    --env RG_RT_SHM=/dev/shm/TeknoParrot_JvsState --env RG_RT_LOG=1 \
    > "$OUT/play.out" 2>&1; echo "$?" > "$OUT/exit" ) &
RUN=$!
for i in $(seq 1 9); do
  sleep 10
  N=$(wc -l < "$G" 2>/dev/null)
  PID=$(pgrep -x game 2>/dev/null | head -1)
  echo "t=$((i*10))s  gamelog=$N regels  game-pid=${PID:-geen}  taplog=$(wc -l < "$TAPLOG" 2>/dev/null || echo 0)" | tee -a "$OUT/leven"
  kill -0 $RUN 2>/dev/null || break
done
wait $RUN 2>/dev/null
echo "  exitcode: $(cat "$OUT/exit" 2>/dev/null)"
date '+  eindtijd: %H:%M:%S'

echo
echo "############ 2. de nieuwe regels met tijdstempels"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | head -8 | sed 's/^/  /'
echo "  --- de launcherlogboek-naam (dat is mijn t0):"
ls -t ~/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | head -1 | sed 's/^/    /'
echo "  --- en de displayregel uit dat logboek:"
grep -h 'display=' ~/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | tail -1 | sed 's/^/    /'

echo
echo "############ 3. opruimen"
pkill -f '/pm/g7/halo/game' 2>/dev/null && echo "  spelproces gestopt" || echo "  geen spelproces meer"
pkill -f 'play-native.sh' 2>/dev/null && echo "  launcher gestopt" || echo "  geen launcher meer"
sleep 2
pgrep -a -f 'gamescope|wineserver|play-native|/pm/g7/halo/game' 2>/dev/null | grep -v pgrep | sed 's/^/  draait nog: /'
echo "  (leeg = schoon)"
cp -a "$G" "$P/gamelog-tijdrun.txt" 2>/dev/null && echo "  bewaard: $P/gamelog-tijdrun.txt"
echo "== done ($OUT)"
