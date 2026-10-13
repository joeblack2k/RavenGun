#!/bin/bash
# RUN 9 - STAP 1: opent de game ons apparaat? Geen tracer: alleen /proc/<pid>/fd lezen.
# En meteen: is SDL eigenlijk wel in het proces? (de game lost zijn invoer via dlopen op)
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
D=~/.local/share/ravengun
TAPDIR="$D/tap"
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
JVS="$TAPDIR/start.jvs"
PROBE="$TAPDIR/probe.txt"
OUT=$(mktemp -d /tmp/run9-XXXX)
REGELS_VOOR=$(wc -l < "$G" 2>/dev/null)
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
CTL="$R/build/ravengunctl"

maak_foto() { local doel="$OUT/$1.png"
  command -v spectacle >/dev/null && spectacle -b -n -o "$doel" >/dev/null 2>&1 && [ -s "$doel" ]; }
bit() { python3 -c "
import os, struct
fd = os.open('$JVS', os.O_WRONLY); os.pwrite(fd, struct.pack('<I', $1), 8); os.close(fd)"; }
druk() { bit 0; sleep 0.5; bit $1; sleep 2; bit 0; }

echo "############ 0. vooraf"
date '+  starttijd: %H:%M:%S'; pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game' | grep -v pgrep | sed 's/^/  draait al: /'; echo "  (leeg = vrij)"
"$CTL" arm 2>&1 | head -2 | sed 's/^/  arm: /'

echo
echo "############ 1. de run (150 s, beeld op de tv)"
python3 -c "open('$JVS','wb').write(bytes(64))"; printf '0,0,4' > "$PROBE"
cd "$R"
( timeout 300 scripts/play-native.sh --game Halo --gamescope --mute --seconds 150 \
    --bind-ro "$P/probe/libusb-tap.so" /rg/lib/libusb-tap.so \
    --bind-ro "$JVS" /rg/jvs --bind-ro "$PROBE" /rg/tap/probe.txt --bind-rw "$TAPDIR" /rg/tap \
    --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
    --env RG_USB_LOG=/rg/tap/usb-tap.log --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1 \
    --env RG_RT_PULSE_MS=2000 --env RG_RT_PROBE=/rg/tap/probe.txt \
    > "$OUT/play.out" 2>&1; echo "$?" > "$OUT/exit" ) &
RUN=$!
sleep 12
for i in $(seq 1 8); do
  echo "  t=$((SECONDS))s START-puls $i" | tee -a "$OUT/leven"
  druk 64
  grep -q 'FileTest Reported' "$G" 2>/dev/null && { echo "  *** FileTest geeindigd" | tee -a "$OUT/leven"; break; }
  sleep 1
done
sleep 25   # de webcam- en netwerkstap, en dan staat het kalibratiescherm er

PID=$(pgrep -x game 2>/dev/null | head -1)
echo
echo "############ 2. DE METING: welke invoerapparaten houdt de game OPEN? (pid ${PID:-geen})"
if [ -n "${PID:-}" ]; then
  echo "  --- alle open /dev/input- en /dev/hidraw-koppelingen:"
  ls -l "/proc/$PID/fd" 2>/dev/null | grep -E '/dev/input|/dev/hidraw' | sed 's/^/    /'
  echo "  --- en geteld:"
  printf '    /dev/input/event : %s\n' "$(ls -l /proc/$PID/fd 2>/dev/null | grep -c '/dev/input/event')"
  printf '    /dev/input/js    : %s\n' "$(ls -l /proc/$PID/fd 2>/dev/null | grep -c '/dev/input/js')"
  printf '    /dev/hidraw      : %s\n' "$(ls -l /proc/$PID/fd 2>/dev/null | grep -c '/dev/hidraw')"
  echo "  --- is ONS apparaat (event13) erbij?"
  ls -l "/proc/$PID/fd" 2>/dev/null | grep -c 'event13' | sed 's/^/    event13: /'
  echo "  --- welke namen horen bij de geopende event-nodes?"
  for fd in $(ls "/proc/$PID/fd" 2>/dev/null); do
    tgt=$(readlink "/proc/$PID/fd/$fd" 2>/dev/null) || continue
    case "$tgt" in /dev/input/event*) n=$(cat "/sys/class/input/$(basename "$tgt")/device/name" 2>/dev/null); printf '    fd %-4s %-22s %s\n' "$fd" "$tgt" "$n";; esac
  done
  echo "  --- is SDL in het proces gemapt? (de game lost invoer via dlopen op)"
  printf '    SDL-mappings : %s\n' "$(grep -ci sdl /proc/$PID/maps 2>/dev/null)"
  printf '    udev-mappings: %s\n' "$(grep -ci udev /proc/$PID/maps 2>/dev/null)"
  printf '    libinput     : %s\n' "$(grep -ci libinput /proc/$PID/maps 2>/dev/null)"
  echo "  --- en de geladen bibliotheken met 'input' of 'sdl' in de naam:"
  grep -oiE '/[^ ]*(sdl|input|udev)[^ ]*\.so[^ ]*' /proc/$PID/maps 2>/dev/null | sort -u | head -8 | sed 's/^/    /'
  echo "  --- alle open fds naar het virtuele lichtgeweer of toetsenbord:"
  ls -l "/proc/$PID/fd" 2>/dev/null | grep -E 'RavenGun|ravengun' | sed 's/^/    /'
else
  echo "  *** geen game-proces gevonden"
fi

echo
echo "############ 3. nu nog een keer positie + trekker, ter controle"
"$CTL" test-pattern point 0 0 >/dev/null 2>&1; sleep 2
"$CTL" output-button trigger tap >/dev/null 2>&1; sleep 3
maak_foto "kalibratie-na-trekker"
echo "  gamelog: $(wc -l < "$G") regels (was $REGELS_VOOR)"

echo
echo "############ 4. wat de game schreef"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | tail -6 | sed 's/^/  /'

echo
echo "############ 5. opruimen"
pkill -f '/pm/g7/halo/game' 2>/dev/null; pkill -f 'play-native.sh' 2>/dev/null; sleep 2
"$CTL" test-pattern stop >/dev/null 2>&1; "$CTL" disarm >/dev/null 2>&1
pgrep -a -f 'gamescope|wineserver|play-native|/pm/g7/halo/game' 2>/dev/null | grep -v pgrep | sed 's/^/  draait nog: /'; echo "  (leeg = schoon)"
cp -a "$G" "$P/gamelog-run9.txt" 2>/dev/null
cp -a "$OUT/leven" "$P/run9-leven" 2>/dev/null
for p in "$OUT"/*.png; do [ -f "$p" ] && cp -a "$p" "$P/run9-$(basename "$p")"; done
echo "  --- ruwe rommel: $OUT"
echo "== done"
