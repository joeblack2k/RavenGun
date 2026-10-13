#!/bin/bash
# RUN 12: DE LAT -- vijf minuten attract-lus vasthouden.
# Route (uit run 11): START (400 ms, herhaald) -> FileTest weg; TEST op ALLE VIER de borden (400 ms)
# voor gun 1; START voor gun 2; dan blijft de attract-lus lopen. Ik laat hem staan en fotografeer
# periodiek, zodat de vijf minuten meetbaar zijn en niet gehoopt.
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
D=~/.local/share/ravengun
TAPDIR="$D/tap"
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
JVS="$TAPDIR/start.jvs"
OUT=$(mktemp -d /tmp/run12-XXXX)
REGELS_VOOR=$(wc -l < "$G" 2>/dev/null)
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"

maak_foto() { local doel="$OUT/$1.png"
  command -v spectacle >/dev/null && spectacle -b -n -o "$doel" >/dev/null 2>&1 && [ -s "$doel" ]; }
bit() { python3 -c "
import os, struct
fd = os.open('$JVS', os.O_WRONLY); os.pwrite(fd, struct.pack('<I', $1), 8); os.close(fd)"; }
druk() { bit 0; sleep 0.3; bit $1; sleep 0.6; bit 0; }

echo "############ 0. vooraf"
date '+  starttijd: %H:%M:%S'; printf '  gamelog had %s regels\n' "$REGELS_VOOR"
pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game' | grep -v pgrep | sed 's/^/  draait al: /'; echo "  (leeg = vrij)"
echo "  --- Move aangesloten? (moet nee zijn)"; grep -l '054c' /sys/class/input/event*/id/vendor 2>/dev/null | sed 's/^/    /'; echo "    (leeg = nee)"

echo
echo "############ 1. de run (420 s, beeld op de tv) -- de attract-lus moet 5 minuten staan"
python3 -c "open('$JVS','wb').write(bytes(64))"
rm -f "$OUT/t0" ; date +%s > "$OUT/t0"
cd "$R"
( timeout 600 scripts/play-native.sh --game Halo --gamescope --mute --seconds 420 \
    --bind-ro "$P/probe/libusb-tap.so" /rg/lib/libusb-tap.so \
    --bind-ro "$JVS" /rg/jvs --bind-rw "$TAPDIR" /rg/tap \
    --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
    --env RG_USB_LOG=/rg/tap/usb-tap.log --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1 \
    --env RG_RT_PULSE_MS=400 \
    > "$OUT/play.out" 2>&1; echo "$?" > "$OUT/exit" ) &
RUN=$!
sleep 12
echo "   --- START herhalen tot de FileTest eindigt"
for i in $(seq 1 30); do
  druk 64
  grep -q 'FileTest Reported' "$G" 2>/dev/null && { echo "  *** FileTest weg na $i pulsen" | tee -a "$OUT/leven"; break; }
  sleep 1
done
sleep 25
echo "   --- TEST op alle vier de borden (gun 1), drie keer"
for i in 1 2 3; do druk 1; sleep 4; done
echo "   --- en START (gun 2), drie keer"
for i in 1 2 3; do druk 64; sleep 4; done
echo "   --- wachten op de attract-bank"
for i in $(seq 1 12); do
  sleep 5
  grep -q 'sound bank: attract' "$G" 2>/dev/null && { echo "  *** DE ATTRACT-BANK IS GELADEN op $(date +%H:%M:%S) (t=$((SECONDS))s)" | tee -a "$OUT/leven"; break; }
done
ATTRACT_T=$(date +%s); echo "$ATTRACT_T" > "$OUT/t_attract"
grep -q 'sound bank: attract' "$G" && echo "  attract-bank: JA" || echo "  attract-bank: NOG NIET"

echo
echo "   --- DE LAT: elke 60 s een foto en de stand van het logboek"
for i in $(seq 1 6); do
  sleep 60
  maak_foto "lat-$i-min"
  printf '  t=%ss (min %s): gamelog=%s regels; banken: %s\n' "$((SECONDS))" "$i" "$(wc -l < "$G")" \
    "$(grep -o 'sound bank: [a-z0-9]*' "$G" | tail -3 | tr '\n' ' ')" | tee -a "$OUT/leven"
  kill -0 $RUN 2>/dev/null || { echo "  (de launcher is klaar)" | tee -a "$OUT/leven"; break; }
done
maak_foto "lat-eind"
wait $RUN 2>/dev/null
EIND_T=$(date +%s); echo "$EIND_T" > "$OUT/t_eind"
echo "  exitcode: $(cat "$OUT/exit" 2>/dev/null)"

echo
echo "############ 2. DE LAT BEREKEND"
python3 - "$OUT/t_attract" "$OUT/t_eind" <<'PY'
import sys
a = int(open(sys.argv[1]).read().strip()); e = int(open(sys.argv[2]).read().strip())
d = e - a
print("  attract-bank om %s, einde om %s -> %d seconden = %.1f minuten in de attract-lus" %
      (a, e, d, d/60.0))
print("  DE LAT (5 minuten = 300 s): %s" % ("GEHAALD" if d >= 300 else "NIET gehaald (te kort)"))
PY

echo
echo "############ 3. de banken en de munt in het logboek"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | grep -iE 'sound bank|Money In|coin pulses|attract|level' | head -12 | sed 's/^/  /'
echo "  --- markers:"
for m in 'attract' 'level1' 'level2' 'Money In' 'QR scanner' 'Factory Setup'; do
  printf '    %-16s %s\n' "$m" "$(tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | grep -ci "$m")"
done

echo
echo "############ 4. opruimen"
pkill -f '/pm/g7/halo/game' 2>/dev/null; pkill -f 'play-native.sh' 2>/dev/null; sleep 2
pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game' 2>/dev/null | grep -v pgrep | sed 's/^/  draait nog: /'; echo "  (leeg = schoon)"
cp -a "$G" "$P/gamelog-run12.txt" 2>/dev/null
cp -a "$OUT/leven" "$P/run12-leven" 2>/dev/null
for p in "$OUT"/*.png; do [ -f "$p" ] && cp -a "$p" "$P/run12-$(basename "$p")"; done
ls -l "$P"/run12-*.png 2>/dev/null | sed 's/^/  /'
echo "  --- ruwe rommel: $OUT"
echo "== done"
