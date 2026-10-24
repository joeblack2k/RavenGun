#!/bin/bash
# RUN 3 voor Halo: 120 s, BEELD OP DE TV (--gamescope in plaats van --headless), dezelfde tap,
# dezelfde START-route via het schakelaarbeeld. Plus schermafbeeldingen, want de eigenaar kijkt mee.
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
D=~/.local/share/ravengun
TAPDIR="$D/tap"
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
TAPLOG="$TAPDIR/usb-tap.log"
JVS="$TAPDIR/start.jvs"
OUT=$(mktemp -d /tmp/run3-XXXX)
REGELS_VOOR=$(wc -l < "$G" 2>/dev/null)

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"

# --- schermafbeelding: welke hulpmiddelen zijn er? ---------------------------------------------
maak_foto() {
  local naam="$1" doel="$OUT/$1.png"
  for tool in "grim" "spectacle -b -n -o" "import -window root"; do
    case "$tool" in
      grim)      command -v grim >/dev/null && grim "$doel" 2>/dev/null && { echo "  foto $naam met grim"; return 0; } ;;
      spectacle*) command -v spectacle >/dev/null && spectacle -b -n -o "$doel" >/dev/null 2>&1 && [ -s "$doel" ] && { echo "  foto $naam met spectacle"; return 0; } ;;
      import*)   command -v import >/dev/null && DISPLAY=:0 import -window root "$doel" 2>/dev/null && [ -s "$doel" ] && { echo "  foto $naam met import (X11 :0)"; return 0; } ;;
    esac
  done
  echo "  foto $naam MISLUKT (grim/spectacle/import alle drie niet gelukt)"
  return 1
}

echo "############ 0. vooraf"
date '+  tijd: %H:%M:%S'
echo "  gamelog had $REGELS_VOOR regels"
printf '  schermhulpmiddelen: grim=%s spectacle=%s import=%s\n' \
  "$(command -v grim || echo nee)" "$(command -v spectacle || echo nee)" "$(command -v import || echo nee)"
pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game' | grep -v pgrep | sed 's/^/  draait al: /'
echo "  (leeg = vrij)"

echo
echo "############ 1. het schakelaarbeeld klaarzetten (64 bytes, alles nul)"
python3 -c "open('$JVS','wb').write(bytes(64))"
ls -l "$JVS" | sed 's/^/  /'
rm -f "$TAPLOG"
: > "$OUT/leven"

echo
echo "############ 2. de run (120 s, BEELD OP DE TV)"
cd "$R"
( timeout 300 scripts/play-native.sh --game Halo --gamescope --mute --seconds 120 \
    --bind-ro "$P/probe/libusb-tap.so" /rg/lib/libusb-tap.so \
    --bind-ro "$JVS" /rg/jvs \
    --bind-rw "$TAPDIR" /rg/tap \
    --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
    --env RG_USB_LOG=/rg/tap/usb-tap.log --env RG_USB_RAW=1 --env RG_USB_MAX=50000 \
    --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1 \
    > "$OUT/play.out" 2>&1; echo "$?" > "$OUT/exit" ) &
RUN=$!

start_bit() { python3 -c "
import struct
p='$JVS'
d=bytearray(open(p,'rb').read())
struct.pack_into('<I', d, 8, $1)
open(p,'wb').write(d)
"; }

# t=15: foto VOOR de indruk
sleep 15
echo "--- t=15s: foto voor de START-indruk" | tee -a "$OUT/leven"
maak_foto "voor-start" | tee -a "$OUT/leven"
GL15=$(wc -l < "$G" 2>/dev/null)

# t=20: de START-indruk (bit 6)
sleep 5
date '+  t=20s: START-bit zetten  %H:%M:%S' | tee -a "$OUT/leven"
start_bit "64"
GL20=$(wc -l < "$G" 2>/dev/null)

# t=30: foto NA de indruk
sleep 10
echo "--- t=30s: foto na de START-indruk" | tee -a "$OUT/leven"
maak_foto "na-start" | tee -a "$OUT/leven"
echo "  gamelog: $GL20 regels bij de indruk, nu $(wc -l < "$G" 2>/dev/null)" | tee -a "$OUT/leven"

# t=60: tweede indruk als er niets gebeurd is
sleep 30
GL60=$(wc -l < "$G" 2>/dev/null)
if [ "$GL60" -le "$GL20" ]; then
  date '+  t=60s: nog niets gebeurd; bit wissen en opnieuw zetten  %H:%M:%S' | tee -a "$OUT/leven"
  start_bit 0; sleep 1; start_bit 64
else
  echo "  t=60s: de game is verder gegaan ($GL20 -> $GL60 regels); geen tweede indruk nodig" | tee -a "$OUT/leven"
fi
sleep 10
echo "--- t=70s: foto" | tee -a "$OUT/leven"
maak_foto "t70" | tee -a "$OUT/leven"

# levenscheck tot het einde
for i in $(seq 1 5); do
  sleep 10
  PID=$(pgrep -x game 2>/dev/null | head -1)
  if [ -n "${PID:-}" ]; then
    echo "t=$((70+i*10))s  game-pid=$PID threads=$(ls /proc/$PID/task 2>/dev/null | wc -l) fmod=$(grep -c fmod /proc/$PID/maps 2>/dev/null)  gamelog=$(wc -l < "$G")  taplog=$(wc -l < "$TAPLOG" 2>/dev/null || echo 0)" | tee -a "$OUT/leven"
  else
    echo "t=$((70+i*10))s  geen 'game'-proces meer; gamelog=$(wc -l < "$G")" | tee -a "$OUT/leven"
  fi
  kill -0 $RUN 2>/dev/null || { echo "  (de launcher is klaar)" | tee -a "$OUT/leven"; break; }
done
wait $RUN 2>/dev/null
echo "  exitcode van de launcher: $(cat "$OUT/exit" 2>/dev/null)"
date '+  tijd: %H:%M:%S'

echo
echo "############ 3. WAT DE GAME ZELF SCHREEF (de autoriteit), met tijdstempels"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | sed 's/^/  /'
echo "  --- eerste nieuwe regel:"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | head -1 | sed 's/^/    /'
echo "  --- FileTest-regel(s):"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | grep -i filetest | sed 's/^/    /'
echo "  (leeg = geen FileTest-regel)"

echo
echo "############ 4. HET LOGBOEK VAN DE TAP, en de indruk-meldingen van de hulp"
if [ -f "$TAPLOG" ]; then
  echo "  regels: $(wc -l < "$TAPLOG")"
  for s in 'tap start' 'attr idVendor' 'attr idProduct' 'attr busnum' 'APPARAAT' 'DIT IS HET FTDI-BORD' 'descriptors' 'ctrl' 'bulk'; do
    printf '    %-26s %s\n' "$s" "$(grep -c "$s" "$TAPLOG")"
  done
  echo "  --- de eerste 8 en laatste 4 regels:"
  head -8 "$TAPLOG" | sed 's/^/    /'
  tail -4 "$TAPLOG" | sed 's/^/    /'
else
  echo "  *** GEEN tap-logboek -- uitkomst (i): de tap is niet geladen. Proeffout, geen meting."
fi

echo
echo "############ 5. wat de launcher zelf meldde (display, gamescope)"
grep -E 'play-native: |node ID|gamescope.*(Error|error|fail)' "$OUT/play.out" | head -8 | sed 's/^/  /'
echo "  --- en de kop van het launcherlogboek:"
grep -h 'display=' "$OUT/play.out" 2>/dev/null | head -2 | sed 's/^/    /'
ls -t ~/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | head -1 | sed 's/^/    nieuwste: /'

echo
echo "############ 6. opruimen"
pkill -f '/pm/g7/halo/game' 2>/dev/null && echo "  spelproces gestopt" || echo "  geen spelproces meer"
pkill -f 'play-native.sh' 2>/dev/null && echo "  launcher gestopt" || echo "  geen launcher meer"
sleep 2
echo "  --- wat er nog draait:"
pgrep -a -f 'gamescope|wineserver|play-native|/pm/g7/halo/game' 2>/dev/null | grep -v pgrep | sed 's/^/    /'
echo "  (leeg = schoon)"

echo
echo "############ 7. bewaren"
for f in "$TAPLOG"; do [ -f "$f" ] && cp -a "$f" "$P/usb-tap-run3.log" && echo "  $P/usb-tap-run3.log"; done
cp -a "$G" "$P/gamelog-run3.txt" 2>/dev/null && echo "  $P/gamelog-run3.txt"
for p in "$OUT"/*.png; do [ -f "$p" ] && cp -a "$p" "$P/foto-$(basename "$p")" && echo "  $P/foto-$(basename "$p")"; done
ls -l "$P"/foto-*.png 2>/dev/null | sed 's/^/  /'
echo "  --- ruwe rommel: $OUT"
echo "== done"
