#!/bin/bash
# RUN 5 voor Halo: de pulsrun. Beeld op de tv, dezelfde tap en levenscheck, en nu:
#   RG_RT_PULSE_MS=2000   -- een lange puls, zodat de scanner hem niet kan missen
#   t=20  START (bit 6)
#   t=55  TEST (bit 0) via RG_RT_PROBE=0,0,4 -> ALLEEN bord 0, niet vier borden
#   t=85  TEST (bit 0) opnieuw
# Mijn pulsen schrijven OP ZIJN PLAATS (geen "wb" meer): dat afkappen was de oorzaak van de crash.
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
D=~/.local/share/ravengun
TAPDIR="$D/tap"
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
TAPLOG="$TAPDIR/usb-tap.log"
JVS="$TAPDIR/start.jvs"
PROBE="$TAPDIR/probe.txt"
OUT=$(mktemp -d /tmp/run5-XXXX)
REGELS_VOOR=$(wc -l < "$G" 2>/dev/null)
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"

echo "############ 0. vooraf: mijn eigen rommel van de proef opruimen"
pkill -f 'puls-.*/lezer' 2>/dev/null && echo "  mijn proeflezer gestopt" || echo "  geen proeflezer meer"
pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game' | grep -v pgrep | sed 's/^/  draait al: /'
echo "  (leeg = vrij)"
date '+  tijd: %H:%M:%S'

maak_foto() {
  local doel="$OUT/$1.png"
  command -v spectacle >/dev/null && spectacle -b -n -o "$doel" >/dev/null 2>&1 && [ -s "$doel" ] && { echo "  foto $1 gemaakt"; return 0; }
  echo "  foto $1 MISLUKT"; return 1
}
# IN PLACE schrijven: nooit afkappen (dat was de crash)
bit() { python3 -c "
import os, struct
fd = os.open('$JVS', os.O_WRONLY)
os.pwrite(fd, struct.pack('<I', $1), 8)
os.close(fd)
"; }

echo
echo "############ 1. schakelaarbeeld (64 B) en het probe-bestand voor TEST op bord 0"
python3 -c "open('$JVS','wb').write(bytes(64))"
printf '0,0,4' > "$PROBE"
ls -l "$JVS" "$PROBE" | sed 's/^/  /'
rm -f "$TAPLOG"; : > "$OUT/leven"

echo
echo "############ 2. de run (150 s, beeld op de tv, RG_RT_PULSE_MS=2000)"
cd "$R"
( timeout 330 scripts/play-native.sh --game Halo --gamescope --mute --seconds 150 \
    --bind-ro "$P/probe/libusb-tap.so" /rg/lib/libusb-tap.so \
    --bind-ro "$JVS" /rg/jvs \
    --bind-ro "$PROBE" /rg/tap/probe.txt \
    --bind-rw "$TAPDIR" /rg/tap \
    --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
    --env RG_USB_LOG=/rg/tap/usb-tap.log --env RG_USB_RAW=1 --env RG_USB_MAX=50000 \
    --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1 --env RG_RT_PULSE_MS=2000 \
    --env RG_RT_PROBE=/rg/tap/probe.txt \
    > "$OUT/play.out" 2>&1; echo "$?" > "$OUT/exit" ) &
RUN=$!

sleep 15; maak_foto "t15-voor" | tee -a "$OUT/leven"
sleep 5
date '+  t=20s: START (bit 6, puls 2000 ms)  %H:%M:%S' | tee -a "$OUT/leven"; bit 64
sleep 20; maak_foto "t40-na-start" | tee -a "$OUT/leven"
echo "  tussenstand t=40s: gamelog $(wc -l < "$G") regels" | tee -a "$OUT/leven"
sleep 15
date '+  t=55s: TEST 1 (bit 0, bord 0)  %H:%M:%S' | tee -a "$OUT/leven"
bit 0; sleep 1; bit 1
sleep 3; echo "  gamelog nu $(wc -l < "$G") regels" | tee -a "$OUT/leven"
sleep 12; maak_foto "t70-na-test1" | tee -a "$OUT/leven"
sleep 15
date '+  t=85s: TEST 2 (bit 0, bord 0)  %H:%M:%S' | tee -a "$OUT/leven"
bit 0; sleep 1; bit 1
sleep 3; echo "  gamelog nu $(wc -l < "$G") regels" | tee -a "$OUT/leven"
sleep 27; maak_foto "t115-na-test2" | tee -a "$OUT/leven"

for i in $(seq 1 4); do
  sleep 10
  PID=$(pgrep -x game 2>/dev/null | head -1)
  if [ -n "${PID:-}" ]; then
    echo "t=$((115+i*10))s  game-pid=$PID threads=$(ls /proc/$PID/task 2>/dev/null | wc -l) fmod=$(grep -c fmod /proc/$PID/maps 2>/dev/null)  gamelog=$(wc -l < "$G")  taplog=$(wc -l < "$TAPLOG" 2>/dev/null || echo 0)" | tee -a "$OUT/leven"
  else
    echo "t=$((115+i*10))s  geen 'game'-proces meer; gamelog=$(wc -l < "$G")" | tee -a "$OUT/leven"
  fi
  kill -0 $RUN 2>/dev/null || { echo "  (de launcher is klaar)" | tee -a "$OUT/leven"; break; }
done
wait $RUN 2>/dev/null
echo "  exitcode: $(cat "$OUT/exit" 2>/dev/null)"
date '+  tijd: %H:%M:%S'

echo
echo "############ 3. WAT DE GAME SCHREEF, met tijdstempels (de autoriteit)"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | sed 's/^/  /'

echo
echo "############ 4. ZIJN DE PULSEN AANGEKOMEN, en is er een crash?"
NAT=$(ls -t ~/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | head -1)
grep 'indruk' "$NAT" 2>/dev/null | sed 's/^/  /'
echo "  --- aantal indruk-meldingen: $(grep -c 'indruk' "$NAT" 2>/dev/null || echo 0)"
echo "  --- en is er gecrasht?"
if grep -qE 'FATAL signal|Program exiting' "$G" 2>/dev/null; then
  echo "  *** JA -- de stack trace:"
  grep -A12 'FATAL signal' "$G" | sed 's/^/    /'
else
  echo "  nee: geen FATAL signal in het logboek van de game"
fi

echo
echo "############ 5. het tap-logboek"
if [ -f "$TAPLOG" ]; then
  for s in 'tap start' 'attr idVendor' 'attr idProduct' 'APPARAAT' 'DIT IS HET FTDI-BORD' 'descriptors' 'ctrl'; do
    printf '  %-24s %s\n' "$s" "$(grep -c "$s" "$TAPLOG")"
  done
else
  echo "  *** geen logboek (proeffout)"
fi

echo
echo "############ 6. opruimen"
pkill -f '/pm/g7/halo/game' 2>/dev/null && echo "  spelproces gestopt" || echo "  geen spelproces meer"
pkill -f 'play-native.sh' 2>/dev/null && echo "  launcher gestopt" || echo "  geen launcher meer"
pkill -f 'puls-.*/lezer' 2>/dev/null
sleep 2
pgrep -a -f 'gamescope|wineserver|play-native|/pm/g7/halo/game|lezer' 2>/dev/null | grep -v pgrep | sed 's/^/  draait nog: /'
echo "  (leeg = schoon)"

echo
echo "############ 7. bewaren"
[ -f "$TAPLOG" ] && cp -a "$TAPLOG" "$P/usb-tap-run5.log" && echo "  usb-tap-run5.log"
cp -a "$G" "$P/gamelog-run5.txt" 2>/dev/null && echo "  gamelog-run5.txt"
for p in "$OUT"/*.png; do [ -f "$p" ] && cp -a "$p" "$P/run5-$(basename "$p")" && echo "  run5-$(basename "$p")"; done
echo "  --- ruwe rommel: $OUT"
echo "== done"
