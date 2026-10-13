#!/bin/bash
# RUN 4 voor Halo: beeld op de tv, dezelfde tap en invoerhulp, en nu VIER pulsen:
#   t=20  START (bit 6)  -- om de FileTest te passeren (bewezen in run 3)
#   t=60  TEST  (bit 0)  -- kalibratie overslaan
#   t=85  TEST  (bit 0)  -- tweede keer, ruim na de eerste
# De momenten komen uit run 3: FileTest 10:25:45, "Webcam: Aborting auto-id" 10:26:25 (t=41 s),
# kalibratiescherm zichtbaar op t=70 s. Venster 150 s zodat het gevolg van de tweede TEST te zien is.
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
D=~/.local/share/ravengun
TAPDIR="$D/tap"
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
TAPLOG="$TAPDIR/usb-tap.log"
JVS="$TAPDIR/start.jvs"
OUT=$(mktemp -d /tmp/run4-XXXX)
REGELS_VOOR=$(wc -l < "$G" 2>/dev/null)
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"

maak_foto() {
  local doel="$OUT/$1.png"
  command -v spectacle >/dev/null && spectacle -b -n -o "$doel" >/dev/null 2>&1 && [ -s "$doel" ] && {
    echo "  foto $1 gemaakt"; return 0; }
  echo "  foto $1 MISLUKT"; return 1
}
bit() { python3 -c "
import struct
d=bytearray(open('$JVS','rb').read())
struct.pack_into('<I', d, 8, $1)
open('$JVS','wb').write(d)
"; }

echo "############ 0. vooraf"
date '+  tijd: %H:%M:%S'
echo "  gamelog had $REGELS_VOOR regels"
pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game' | grep -v pgrep | sed 's/^/  draait al: /'
echo "  (leeg = vrij)"

echo
echo "############ 1. het schakelaarbeeld op nul, en het tap-logboek leeg"
bit 0; ls -l "$JVS" | sed 's/^/  /'
rm -f "$TAPLOG"; : > "$OUT/leven"

echo
echo "############ 2. de run (150 s, beeld op de tv)"
cd "$R"
( timeout 330 scripts/play-native.sh --game Halo --gamescope --mute --seconds 150 \
    --bind-ro "$P/probe/libusb-tap.so" /rg/lib/libusb-tap.so \
    --bind-ro "$JVS" /rg/jvs \
    --bind-rw "$TAPDIR" /rg/tap \
    --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
    --env RG_USB_LOG=/rg/tap/usb-tap.log --env RG_USB_RAW=1 --env RG_USB_MAX=50000 \
    --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1 \
    > "$OUT/play.out" 2>&1; echo "$?" > "$OUT/exit" ) &
RUN=$!

sleep 15; echo "--- t=15s: foto voor de pulsen" | tee -a "$OUT/leven"; maak_foto "t15-voor" | tee -a "$OUT/leven"
sleep 5
date '+  t=20s: START (bit 6)  %H:%M:%S' | tee -a "$OUT/leven"; bit 64
GL_NA_START=0
sleep 20; maak_foto "t40-na-start" | tee -a "$OUT/leven"
echo "  tussenstand: gamelog $(wc -l < "$G") regels" | tee -a "$OUT/leven"
sleep 20
date '+  t=60s: TEST 1 (bit 0)  %H:%M:%S' | tee -a "$OUT/leven"; bit 1
sleep 3;  echo "  gamelog nu $(wc -l < "$G") regels" | tee -a "$OUT/leven"
sleep 17; maak_foto "t80-na-test1" | tee -a "$OUT/leven"
echo "  tussenstand t=80s: gamelog $(wc -l < "$G") regels" | tee -a "$OUT/leven"
sleep 4
# tweede TEST: eerst de bit wissen zodat de flank gegarandeerd is
bit 0; sleep 1
date '+  t=85s: TEST 2 (bit 0)  %H:%M:%S' | tee -a "$OUT/leven"; bit 1
sleep 3;  echo "  gamelog nu $(wc -l < "$G") regels" | tee -a "$OUT/leven"
sleep 27; maak_foto "t115-na-test2" | tee -a "$OUT/leven"
echo "  tussenstand t=115s: gamelog $(wc -l < "$G") regels" | tee -a "$OUT/leven"

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
echo "############ 4. ZIJN DE PULSEN AANGEKOMEN? (de eigen melding van de invoerhulp)"
NAT=$(ls -t ~/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | head -1)
for bron in "$G" "$NAT" "$OUT/play.out"; do
  [ -f "$bron" ] || continue
  n=$(grep -c 'indruk' "$bron" 2>/dev/null || echo 0)
  printf '  %-52s %s regels met "indruk"\n' "$(basename "$bron")" "$n"
  grep 'indruk' "$bron" 2>/dev/null | tail -6 | sed 's/^/    /'
done

echo
echo "############ 5. het tap-logboek"
if [ -f "$TAPLOG" ]; then
  echo "  regels: $(wc -l < "$TAPLOG")"
  for s in 'tap start' 'attr idVendor' 'attr idProduct' 'APPARAAT' 'DIT IS HET FTDI-BORD' 'descriptors' 'ctrl'; do
    printf '    %-24s %s\n' "$s" "$(grep -c "$s" "$TAPLOG")"
  done
else
  echo "  *** GEEN logboek -- de tap is niet geladen (proeffout)"
fi

echo
echo "############ 6. opruimen"
pkill -f '/pm/g7/halo/game' 2>/dev/null && echo "  spelproces gestopt" || echo "  geen spelproces meer"
pkill -f 'play-native.sh' 2>/dev/null && echo "  launcher gestopt" || echo "  geen launcher meer"
sleep 2
pgrep -a -f 'gamescope|wineserver|play-native|/pm/g7/halo/game' 2>/dev/null | grep -v pgrep | sed 's/^/  draait nog: /'
echo "  (leeg = schoon)"

echo
echo "############ 7. bewaren"
[ -f "$TAPLOG" ] && cp -a "$TAPLOG" "$P/usb-tap-run4.log" && echo "  usb-tap-run4.log"
cp -a "$G" "$P/gamelog-run4.txt" 2>/dev/null && echo "  gamelog-run4.txt"
for p in "$OUT"/*.png; do [ -f "$p" ] && cp -a "$p" "$P/run4-$(basename "$p")" && echo "  run4-$(basename "$p")"; done
echo "  --- ruwe rommel: $OUT"
echo "== done"
