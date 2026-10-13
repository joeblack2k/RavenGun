#!/bin/bash
# RUN 11: DE VIER-BORDEN-VARIANT -- de route die nooit schoon gemeten is.
#   (a) TEST op ALLE VIER de borden, ZONDER RG_RT_PROBE (de standaardtabel van de hulp), herhaald,
#       foto per poging + de tafelmeting van bord 0 t/m 3 ernaast.  Verwachting: gun 1 verdwijnt.
#   (b) dan START voor gun 2.   (c) dan de attract-lus + munt.   De Move blijft UIT.
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
D=~/.local/share/ravengun
TAPDIR="$D/tap"
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
JVS="$TAPDIR/start.jvs"
OUT=$(mktemp -d /tmp/run11-XXXX)
REGELS_VOOR=$(wc -l < "$G" 2>/dev/null)
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
TABEL=$((0x180ae80 + 0xaea0))
BORDSTRIDE=$((0x2200))
K4OFF=$((4*0x50))

maak_foto() { local doel="$OUT/$1.png"
  command -v spectacle >/dev/null && spectacle -b -n -o "$doel" >/dev/null 2>&1 && [ -s "$doel" ]; }
bit() { python3 -c "
import os, struct
fd = os.open('$JVS', os.O_WRONLY); os.pwrite(fd, struct.pack('<I', $1), 8); os.close(fd)"; }
# een indruk: flank omhoog, puls van ~400 ms (RG_RT_PULSE_MS), dan terug
druk() { bit 0; sleep 0.3; bit $1; sleep 0.6; bit 0; }
tafel() { python3 - "$1" "$TABEL" "$BORDSTRIDE" "$K4OFF" <<'PY'
import struct, sys
pid, tabel, stride, k4 = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
with open("/proc/%d/mem" % pid, "rb", 0) as f:
    for b in range(4):
        f.seek(tabel + b*stride + k4)
        d = f.read(0x20)
        print("    bord %d k=4: %s" % (b, " ".join("%08x" % x for x in struct.unpack("<8I", d))))
PY
}

echo "############ 0. vooraf (en de Move BLIJFT UIT)"
date '+  starttijd: %H:%M:%S'; printf '  gamelog had %s regels\n' "$REGELS_VOOR"
echo "  --- draait er iets (native EN wine)?"
pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game|wine|wineserver|OpenParrotLoader' | grep -v pgrep | head -4 | sed 's/^/    /'
echo "    (een wineserver zonder game/lader = residu, geen run)"
echo "  --- is er een Move aangesloten? (moet NEE zijn)"
grep -l '054c' /sys/class/input/event*/device/id/vendor 2>/dev/null | sed 's/^/    /'
echo "    (leeg = geen Move, precies zoals bedoeld)"
echo "  --- de hulp: staat RG_RT_PROBE UIT? (die overschreef de vier-borden-indruk)"
grep -c 'RG_RT_PROBE' ~/projects/ravengun/tools/rt-input-shim/rg_rt_input.c | sed 's/^/    voorkomens in de bron: /'
echo "    wij geven hem deze run NIET mee"

echo
echo "############ 1. de run (200 s, beeld op de tv, RG_RT_PULSE_MS=400)"
python3 -c "open('$JVS','wb').write(bytes(64))"
rm -f "$TAPDIR/usb-tap.log"; : > "$OUT/leven"
cd "$R"
( timeout 360 scripts/play-native.sh --game Halo --gamescope --mute --seconds 200 \
    --bind-ro "$P/probe/libusb-tap.so" /rg/lib/libusb-tap.so \
    --bind-ro "$JVS" /rg/jvs --bind-rw "$TAPDIR" /rg/tap \
    --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
    --env RG_USB_LOG=/rg/tap/usb-tap.log --env RG_USB_RAW=1 --env RG_USB_MAX=50000 \
    --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1 --env RG_RT_PULSE_MS=400 \
    > "$OUT/play.out" 2>&1; echo "$?" > "$OUT/exit" ) &
RUN=$!
sleep 12

echo "   --- START herhalen (korte pulsen, vaak) tot de FileTest eindigt"
for i in $(seq 1 40); do
  druk 64
  if grep -q 'FileTest Reported' "$G" 2>/dev/null; then
    echo "  *** FileTest geeindigd na $i START-pulsen (t=$((SECONDS))s)" | tee -a "$OUT/leven"; break
  fi
  sleep 1
done
grep -q 'FileTest Reported' "$G" && echo "  FileTest Reported staat erin" | tee -a "$OUT/leven" || echo "  LET OP: geen FileTest Reported" | tee -a "$OUT/leven"
sleep 25
PID=$(pgrep -x game 2>/dev/null | head -1)
echo "  game-pid=${PID:-geen}; kalibratiescherm zou er moeten staan (t=$((SECONDS))s)" | tee -a "$OUT/leven"
maak_foto "a0-kalibratie"
echo "  --- de tafel van bord 0 t/m 3, k=4 (test), VOOR de reeks:"
tafel "$PID" | tee -a "$OUT/leven"

echo
echo "   --- (a) TEST OP ALLE VIER DE BORDEN, herhaald"
for i in $(seq 1 6); do
  echo "  t=$((SECONDS))s TEST-puls $i (alle vier borden)" | tee -a "$OUT/leven"
  druk 1
  sleep 6
  maak_foto "a$i-na-test" | tee -a "$OUT/leven"
  echo "    gamelog: $(wc -l < "$G") regels" | tee -a "$OUT/leven"
done
echo "  --- de tafel NA de reeks:"
tafel "$PID" | tee -a "$OUT/leven"

echo
echo "   --- (b) START voor de kalibratie van gun 2"
for i in $(seq 1 4); do
  echo "  t=$((SECONDS))s START-puls $i" | tee -a "$OUT/leven"
  druk 64
  sleep 6
  maak_foto "b$i-na-start" | tee -a "$OUT/leven"
  echo "    gamelog: $(wc -l < "$G") regels" | tee -a "$OUT/leven"
done

echo
echo "   --- (c) munt (bit 2), een paar keer"
for i in $(seq 1 4); do
  echo "  t=$((SECONDS))s munt-puls $i" | tee -a "$OUT/leven"
  druk 4
  sleep 5
done
maak_foto "c1-na-munt" | tee -a "$OUT/leven"
echo "  gamelog aan het eind: $(wc -l < "$G") regels" | tee -a "$OUT/leven"
wait $RUN 2>/dev/null
echo "  exitcode: $(cat "$OUT/exit" 2>/dev/null)"

echo
echo "############ 2. WAT DE GAME SCHREEF (nieuwe regels, met tijdstempels)"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | sed 's/^/  /'

echo
echo "############ 3. ZIT HET KEERPUNT ERIN? (de markers van de attract-lus)"
for m in 'Money In' 'JOIN NOW' 'Cost to play' 'PRESS START' 'attract' 'level1' 'CHOOSE A GAME MODE' 'MISSION'; do
  printf '  %-22s %s\n' "$m" "$(tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | grep -ci "$m")"
done

echo
echo "############ 4. de indrukken die de hulp meldde"
NAT=$(ls -t ~/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | head -1)
echo "  --- aantal indrukken: $(grep -c 'indruk bit' "$NAT" 2>/dev/null || echo 0)"
grep 'indruk bit' "$NAT" 2>/dev/null | head -14 | sed 's/^/    /'
echo "  --- welke borden kwamen langs? (uit de meldingen)"
grep -o 'indruk bit [0-9]* -> bord [0-9]* schakelaar [0-9]*' "$NAT" 2>/dev/null | sort | uniq -c | sed 's/^/    /'

echo
echo "############ 5. crash?"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | grep -c 'FATAL signal' | sed 's/^/  FATAL-regels: /'

echo
echo "############ 6. opruimen"
pkill -f '/pm/g7/halo/game' 2>/dev/null; pkill -f 'play-native.sh' 2>/dev/null; sleep 2
pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game' 2>/dev/null | grep -v pgrep | sed 's/^/  draait nog: /'; echo "  (leeg = schoon)"
cp -a "$G" "$P/gamelog-run11.txt" 2>/dev/null
cp -a "$OUT/leven" "$P/run11-leven" 2>/dev/null
[ -f "$TAPDIR/usb-tap.log" ] && cp -a "$TAPDIR/usb-tap.log" "$P/usb-tap-run11.log"
for p in "$OUT"/*.png; do [ -f "$p" ] && cp -a "$p" "$P/run11-$(basename "$p")"; done
ls -l "$P"/run11-*.png 2>/dev/null | sed 's/^/  /'
echo "  --- ruwe rommel: $OUT"
echo "== done"
