#!/bin/bash
# RUN 2 voor Halo: 300 s, aangescherpte tap, logboek BUITEN de game-map, levenscheck op het eigen
# logboek van de game plus de fmod-mapping.
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
D=~/.local/share/ravengun
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
TAPLOG="$D/tap/usb-tap.log"
OUT=$(mktemp -d /tmp/run2-XXXX)
REGELS_VOOR=$(wc -l < "$G" 2>/dev/null)

echo "############ 0. vooraf"
date '+  tijd: %H:%M:%S'
echo "  gamelog had $REGELS_VOOR regels"
rm -f "$TAPLOG"
: > "$OUT/leven"

echo
echo "############ 1. de run (300 s, headless)"
cd "$R"
( timeout 420 scripts/play-native.sh --game Halo --headless --seconds 300 \
    --bind-ro "$P/probe/libusb-tap.so" /rg/lib/libusb-tap.so \
    --bind-rw "$D/tap" /rg/tap \
    --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
    --env RG_USB_LOG=/rg/tap/usb-tap.log \
    --env RG_USB_RAW=1 --env RG_USB_MAX=50000 \
    > "$OUT/play.out" 2>&1; echo "$?" > "$OUT/exit" ) &
RUN=$!
for i in $(seq 1 20); do
  sleep 20
  PID=$(pgrep -x game 2>/dev/null | head -1)
  if [ -n "${PID:-}" ]; then
    TH=$(ls /proc/$PID/task 2>/dev/null | wc -l)
    FM=$(grep -c fmod /proc/$PID/maps 2>/dev/null)
    GL=$(wc -l < "$G" 2>/dev/null)
    TL=$(wc -l < "$TAPLOG" 2>/dev/null || echo 0)
    NIEUW=$(tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | tail -1 | cut -c1-70)
    echo "t=$((i*20))s  game-pid=$PID threads=$TH fmod-mappings=$FM  gamelog=$GL regels  taplog=$TL regels  laatste: $NIEUW" | tee -a "$OUT/leven"
  else
    GL=$(wc -l < "$G" 2>/dev/null)
    TL=$(wc -l < "$TAPLOG" 2>/dev/null || echo 0)
    echo "t=$((i*20))s  geen 'game'-proces; gamelog=$GL regels, taplog=$TL regels" | tee -a "$OUT/leven"
  fi
  kill -0 $RUN 2>/dev/null || { echo "  (de launcher is klaar)" | tee -a "$OUT/leven"; break; }
done
wait $RUN 2>/dev/null
echo "  exitcode van de launcher: $(cat "$OUT/exit" 2>/dev/null)"
date '+  tijd: %H:%M:%S'

echo
echo "############ 2. WAT DE GAME ZELF SCHREEF (de autoriteit): de nieuwe regels"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | sed 's/^/  /'
echo "  --- de tijdstempel van de eerste nieuwe regel:"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | head -1 | sed 's/^/    /'
echo "  --- staat er een FileTest-regel bij, en welke naam noemt hij?"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | grep -i 'filetest' | sed 's/^/    /'
echo "  (leeg = geen FileTest-regel in deze run)"

echo
echo "############ 3. HET LOGBOEK VAN DE TAP"
if [ -f "$TAPLOG" ]; then
  echo "  regels: $(wc -l < "$TAPLOG")"
  echo "  --- de eerste 6 en de laatste 6:"
  head -6 "$TAPLOG" | sed 's/^/    /'
  echo "    ..."
  tail -6 "$TAPLOG" | sed 's/^/    /'
  echo "  --- de uitslag-tellingen:"
  for s in 'tap start' 'attr idVendor' 'attr idProduct' 'attr busnum' 'APPARAAT' 'DIT IS HET FTDI-BORD' 'descriptors' '/dev/bus/usb' 'ctrl' 'bulk'; do
    printf '    %-26s %s\n' "$s" "$(grep -c "$s" "$TAPLOG")"
  done
  echo "  --- de gevonden apparaten:"
  grep 'APPARAAT' "$TAPLOG" | sed 's/^/    /'
else
  echo "  *** GEEN logboek op $TAPLOG -- dan is uitkomst (i): de tap is niet geladen, of --bind-rw"
  echo "      heeft het bestand niet doorgegeven. Dit is een fout in de proef, geen meting."
fi

echo
echo "############ 4. de uitvoer van de launcher (zonder de xkb-ruis)"
grep -vE 'xkbcomp|Warning:|Errors from|^\s*$|Virtual modifier|Using 0|X11 cannot|Unsupported maximum' \
  "$OUT/play.out" | tail -12 | sed 's/^/  /'

echo
echo "############ 5. opruimen (alleen MIJN processen)"
pkill -f '/pm/g7/halo/game' 2>/dev/null && echo "  spelproces gestopt" || echo "  geen spelproces meer"
pkill -f 'play-native.sh' 2>/dev/null && echo "  launcher gestopt" || echo "  geen launcher meer"
sleep 2
echo "  --- wat er nu nog draait:"
pgrep -a -f 'gamescope|wineserver|play-native|/pm/g7/halo/game' 2>/dev/null | grep -v pgrep | sed 's/^/    /'
echo "  (leeg = schoon)"

echo
echo "############ 6. het logboek bewaren in de patches-repo"
if [ -f "$TAPLOG" ]; then
  cp -a "$TAPLOG" "$P/usb-tap-run2.log"
  printf '  bewaard: %s (%s regels)\n' "$P/usb-tap-run2.log" "$(wc -l < "$P/usb-tap-run2.log")"
fi
cp -a "$G" "$P/gamelog-run2.txt" 2>/dev/null && echo "  game-logboek bewaard: $P/gamelog-run2.txt"
echo "  --- ruwe rommel: $OUT"
echo "== done"
