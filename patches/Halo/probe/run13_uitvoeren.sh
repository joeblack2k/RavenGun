#!/bin/bash
# RUN 13: DE START-ALLEEN-RUN. 150 s, beeld op de tv, GEEN ENKELE TEST-puls (bit 0 komt niet langs).
# Doel: beslechten of de attract-bank ook ZONDER TEST komt -- en de muntreeks in dezelfde run.
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
D=~/.local/share/ravengun
TAPDIR="$D/tap"
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
JVS="$TAPDIR/start.jvs"
OUT=$(mktemp -d /tmp/run13-XXXX)
REGELS_VOOR=$(wc -l < "$G" 2>/dev/null)
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"

maak_foto() { local doel="$OUT/$1.png"
  command -v spectacle >/dev/null && spectacle -b -n -o "$doel" >/dev/null 2>&1 && [ -s "$doel" ]; }
bit() { python3 -c "
import os, struct
fd = os.open('$JVS', os.O_WRONLY); os.pwrite(fd, struct.pack('<I', $1), 8); os.close(fd)"; }
# alleen START (bit 6 = 64) en munt (bit 2 = 4); TEST (bit 0) wordt NOOIT gezet
start() { bit 0; sleep 0.3; bit 64; sleep 0.6; bit 0; }
munt()  { bit 0; sleep 0.3; bit 4;  sleep 0.6; bit 0; }

echo "############ 0. vooraf"
date '+  starttijd: %H:%M:%S'; printf '  gamelog had %s regels\n' "$REGELS_VOOR"
pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game|wine|OpenParrotLoader' | grep -v pgrep | head -3 | sed 's/^/  draait al: /'; echo "  (leeg = vrij)"
echo "  --- Move aangesloten? (moet nee zijn)"; grep -l '054c' /sys/class/input/event*/id/vendor 2>/dev/null | sed 's/^/    /'; echo "    (leeg = nee)"

echo
echo "############ 1. de run (150 s, beeld op de tv, GEEN TEST-puls)"
python3 -c "open('$JVS','wb').write(bytes(64))"
rm -f "$TAPDIR/usb-tap.log"
cd "$R"
( timeout 300 scripts/play-native.sh --game Halo --gamescope --mute --seconds 150 \
    --bind-ro "$P/probe/libusb-tap.so" /rg/lib/libusb-tap.so \
    --bind-ro "$JVS" /rg/jvs --bind-rw "$TAPDIR" /rg/tap \
    --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
    --env RG_USB_LOG=/rg/tap/usb-tap.log --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1 \
    --env RG_RT_PULSE_MS=400 \
    > "$OUT/play.out" 2>&1; echo "$?" > "$OUT/exit" ) &
RUN=$!
sleep 12

echo "   --- START herhalen tot de FileTest eindigt (alleen bit 6)"
for i in $(seq 1 20); do
  echo "  t=$((SECONDS))s START-puls $i ($(date +%H:%M:%S))" | tee -a "$OUT/leven"
  start
  grep -q 'FileTest Reported' "$G" 2>/dev/null && { echo "  *** FileTest weg na $i pulsen" | tee -a "$OUT/leven"; break; }
  sleep 1
done
sleep 20
maak_foto "01-na-filetstest" | tee -a "$OUT/leven"
echo "  gamelog: $(wc -l < "$G") regels" | tee -a "$OUT/leven"

echo
echo "   --- START blijven geven (dit bracht het in run 11 verder), foto per stap"
for i in $(seq 1 5); do
  echo "  t=$((SECONDS))s START-puls $((i+20)) ($(date +%H:%M:%S))" | tee -a "$OUT/leven"
  start
  sleep 6
  maak_foto "0$((i+1))-na-start$i" | tee -a "$OUT/leven"
  printf '    gamelog=%s regels; banken: %s\n' "$(wc -l < "$G")" \
    "$(grep -o 'sound bank: [a-z0-9]*' "$G" | tail -2 | tr '\n' ' ')" | tee -a "$OUT/leven"
done

echo
echo "   --- wachten op de attract-bank (max 60 s), ZONDER ooit TEST te geven"
for i in $(seq 1 12); do
  sleep 5
  if grep -q 'sound bank: attract' "$G" 2>/dev/null; then
    echo "  *** DE ATTRACT-BANK IS ER op $(date +%H:%M:%S) (t=$((SECONDS))s) -- ZONDER TEST" | tee -a "$OUT/leven"; break
  fi
done
grep -q 'sound bank: attract' "$G" && echo "  attract ZONDER TEST: JA" | tee -a "$OUT/leven" || echo "  attract ZONDER TEST: NOG NIET" | tee -a "$OUT/leven"
maak_foto "08-bij-attract" | tee -a "$OUT/leven"

echo
echo "   --- de muntreeks (bit 2), 4 keer"
for i in $(seq 1 4); do
  echo "  t=$((SECONDS))s munt-puls $i ($(date +%H:%M:%S))" | tee -a "$OUT/leven"
  munt
  sleep 4
done
maak_foto "09-na-munt" | tee -a "$OUT/leven"
sleep 10
maak_foto "10-eind" | tee -a "$OUT/leven"
echo "  gamelog aan het eind: $(wc -l < "$G") regels" | tee -a "$OUT/leven"
wait $RUN 2>/dev/null
echo "  exitcode: $(cat "$OUT/exit" 2>/dev/null)"

echo
echo "############ 2. DE BANKENTIJDLIJN (het scherpste instrument)"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | grep -iE 'sound bank|Money In|coin pulses|Webcam|Network' | sed 's/^/  /'

echo
echo "############ 3. WAT DE HULP MELDDE -- en kwam bit 0 (TEST) voor?"
NAT=$(ls -t ~/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | head -1)
grep 'indruk bit' "$NAT" 2>/dev/null | awk '{print $3, $5, $7, $9}' | sort | uniq -c | sed 's/^/  /'
printf '  aantal TEST-indrukken (bit 0): %s  <- moet 0 zijn\n' "$(grep -c 'indruk bit 0 ' "$NAT" 2>/dev/null || echo 0)"
printf '  aantal START-indrukken (bit 6): %s\n' "$(grep -c 'indruk bit 6 ' "$NAT" 2>/dev/null || echo 0)"
printf '  aantal munt-indrukken (bit 2): %s\n' "$(grep -c 'indruk bit 2 ' "$NAT" 2>/dev/null || echo 0)"

echo
echo "############ 4. DE IDENTITEITSREGEL (bovenaan het logboek)"
head -10 "$NAT" 2>/dev/null | sed 's/^/  /'

echo
echo "############ 5. crash?"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | grep -c 'FATAL signal' | sed 's/^/  FATAL-regels: /'

echo
echo "############ 6. opruimen"
pkill -f '/pm/g7/halo/game' 2>/dev/null; pkill -f 'play-native.sh' 2>/dev/null; sleep 2
pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game' 2>/dev/null | grep -v pgrep | sed 's/^/  draait nog: /'; echo "  (leeg = schoon)"
cp -a "$G" "$P/gamelog-run13.txt" 2>/dev/null
cp -a "$OUT/leven" "$P/run13-leven" 2>/dev/null
cp -a "$NAT" "$P/run13-launcherlog.txt" 2>/dev/null
for p in "$OUT"/*.png; do [ -f "$p" ] && cp -a "$p" "$P/run13-$(basename "$p")"; done
ls -l "$P"/run13-*.png 2>/dev/null | sed 's/^/  /'
echo "  --- ruwe rommel: $OUT"
echo "== done"
