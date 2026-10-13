#!/bin/bash
# RUN 10: twee vragen in een run.
#   A. DE CAMERAKANT: welke camera-apparaten zijn er, ziet de game ze, en opent hij ze?
#   B. TEST VASThOUDEN/HERHALEN op het kalibratiescherm, terwijl ik de HELE tafel van bord 0
#      van buiten het proces in de gaten houd: verandert de game iets in de tafel als reactie?
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
D=~/.local/share/ravengun
TAPDIR="$D/tap"
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
JVS="$TAPDIR/start.jvs"
PROBE="$TAPDIR/probe.txt"
OUT=$(mktemp -d /tmp/run10-XXXX)
REGELS_VOOR=$(wc -l < "$G" 2>/dev/null)
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
TABEL=$((0x180ae80 + 0xaea0))

maak_foto() { local doel="$OUT/$1.png"
  command -v spectacle >/dev/null && spectacle -b -n -o "$doel" >/dev/null 2>&1 && [ -s "$doel" ]; }
bit() { python3 -c "
import os, struct
fd = os.open('$JVS', os.O_WRONLY); os.pwrite(fd, struct.pack('<I', $1), 8); os.close(fd)"; }
druk() { bit 0; sleep 0.4; bit $1; sleep 3; bit 0; }   # puls van ~3 s

echo "############ A0. WAT VOOR CAMERA'S ZIJN ER? (buiten de run om)"
ls -l /dev/video* 2>/dev/null | sed 's/^/  /'
for v in /sys/class/video4linux/video*; do
  [ -e "$v" ] || continue
  printf '  %-10s naam=%s\n' "$(basename "$v")" "$(cat "$v/name" 2>/dev/null)"
done
echo "  (leeg = geen camera-apparaten op deze machine)"
command -v v4l2-ctl >/dev/null && v4l2-ctl --list-devices 2>&1 | head -10 | sed 's/^/  /' || echo "  v4l2-ctl niet aanwezig"

echo
echo "############ 1. de run (150 s, beeld op de tv)"
python3 -c "open('$JVS','wb').write(bytes(64))"; printf '0,0,4' > "$PROBE"
rm -f "$TAPDIR/usb-tap.log"
cd "$R"
( timeout 300 scripts/play-native.sh --game Halo --gamescope --mute --seconds 150 \
    --bind-ro "$P/probe/libusb-tap.so" /rg/lib/libusb-tap.so \
    --bind-ro "$JVS" /rg/jvs --bind-ro "$PROBE" /rg/tap/probe.txt --bind-rw "$TAPDIR" /rg/tap \
    --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
    --env RG_USB_LOG=/rg/tap/usb-tap.log --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1 \
    --env RG_RT_PULSE_MS=3000 --env RG_RT_PROBE=/rg/tap/probe.txt \
    > "$OUT/play.out" 2>&1; echo "$?" > "$OUT/exit" ) &
RUN=$!
sleep 12
for i in $(seq 1 8); do
  echo "  t=$((SECONDS))s START-puls $i" | tee -a "$OUT/leven"
  druk 64
  grep -q 'FileTest Reported' "$G" 2>/dev/null && { echo "  *** FileTest geeindigd" | tee -a "$OUT/leven"; break; }
  sleep 1
done
sleep 25
PID=$(pgrep -x game 2>/dev/null | head -1)
echo "  game-pid=${PID:-geen}; kalibratiescherm zou er nu moeten staan" | tee -a "$OUT/leven"
maak_foto "01-kalibratie"

echo
echo "############ A. DE CAMERAKANT TIJDENS DE RUN"
if [ -n "${PID:-}" ]; then
  echo "  --- camera-apparaten op de machine:"
  ls /dev/video* 2>/dev/null | sed 's/^/    /'; [ -z "$(ls /dev/video* 2>/dev/null)" ] && echo "    GEEN /dev/video* op deze machine"
  echo "  --- houdt de game een camera open?"
  ls -l "/proc/$PID/fd" 2>/dev/null | grep -E '/dev/video|/dev/media' | sed 's/^/    /'
  printf '    aantal camera-fds: %s\n' "$(ls -l /proc/$PID/fd 2>/dev/null | grep -cE '/dev/video|/dev/media')"
  echo "  --- en wat de game via udev ziet: de subsystemen waarin hij snuffelt"
  grep -c 'video4linux' /proc/$PID/maps 2>/dev/null | sed 's/^/    (niet zinvol, maps zegt dit niet) /'
  echo "  --- welke libs: libudev wel, libv4l? "
  grep -oE '/[^ ]*v4l[^ ]*\.so[^ ]*' /proc/$PID/maps 2>/dev/null | sort -u | head -5 | sed 's/^/    /'
  echo "    (leeg = geen libv4l; de game doet de camera dan zelf via udev/ioctl)"
fi

echo
echo "############ B. TEST HERHALEN EN VASThOUDEN, met de tafel in de gaten"
echo "  --- de tafel van bord 0 (k=0..12) voor de testreeks, van buiten gelezen:"
python3 - "$PID" "$TABEL" <<'PY'
import struct, sys
pid, tabel = int(sys.argv[1]), int(sys.argv[2])
with open("/proc/%d/mem" % pid, "rb", 0) as f:
    for k in range(13):
        f.seek(tabel + k*0x50)
        d = f.read(0x20)
        print("    k=%-2d %s" % (k, " ".join("%08x" % x for x in struct.unpack("<8I", d))))
PY
for i in $(seq 1 8); do
  echo "  t=$((SECONDS))s TEST-puls $i (3 s vasthouden)" | tee -a "$OUT/leven"
  druk 1
  sleep 4
done
echo "  --- en de tafel NA de testreeks:"
python3 - "$PID" "$TABEL" <<'PY'
import struct, sys
pid, tabel = int(sys.argv[1]), int(sys.argv[2])
with open("/proc/%d/mem" % pid, "rb", 0) as f:
    for k in range(13):
        f.seek(tabel + k*0x50)
        d = f.read(0x20)
        print("    k=%-2d %s" % (k, " ".join("%08x" % x for x in struct.unpack("<8I", d))))
PY
maak_foto "02-na-testreeks"
echo "  gamelog: $(wc -l < "$G") regels (was $REGELS_VOOR)" | tee -a "$OUT/leven"

echo
echo "############ 2. wat de game schreef (nieuwe regels)"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | tail -8 | sed 's/^/  /'

echo
echo "############ 3. opruimen"
pkill -f '/pm/g7/halo/game' 2>/dev/null; pkill -f 'play-native.sh' 2>/dev/null; sleep 2
pgrep -a -f 'gamescope|wineserver|play-native|/pm/g7/halo/game' 2>/dev/null | grep -v pgrep | sed 's/^/  draait nog: /'; echo "  (leeg = schoon)"
cp -a "$G" "$P/gamelog-run10.txt" 2>/dev/null
cp -a "$OUT/leven" "$P/run10-leven" 2>/dev/null
for p in "$OUT"/*.png; do [ -f "$p" ] && cp -a "$p" "$P/run10-$(basename "$p")"; done
echo "  --- ruwe rommel: $OUT"
echo "== done"
