#!/bin/bash
# RUN 15: EERST naar de attract-lus (START herhalen, en dat EXPLICIET in het logboek controleren),
# DAN pas service (bit 1 -> k=3), en zo nodig de volumetoetsen (bit 4/5) als menunavigatie.
# Kort gehouden: een paar minuten.
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
D=~/.local/share/ravengun
TAPDIR="$D/tap"
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
JVS="$TAPDIR/start.jvs"
OUT=$(mktemp -d /tmp/run15-XXXX)
REGELS_VOOR=$(wc -l < "$G" 2>/dev/null)
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"

maak_foto() { local doel="$OUT/$1.png"
  command -v spectacle >/dev/null && spectacle -b -n -o "$doel" >/dev/null 2>&1 && [ -s "$doel" ]; }
bit() { python3 -c "
import os, struct
fd = os.open('$JVS', os.O_WRONLY); os.pwrite(fd, struct.pack('<I', $1), 8); os.close(fd)"; }
druk() { bit 0; sleep 0.3; bit $1; sleep 0.7; bit 0; }   # puls ~700 ms
tafel() { python3 - "$1" <<'PY'
import struct, sys
pid = int(sys.argv[1])
with open("/proc/%d/mem" % pid, "rb", 0) as f:
    for k, naam in ((3, "service"), (0, "start"), (4, "test"), (1, "volume+"), (2, "volume-")):
        f.seek(0x180ae80 + 0xaea0 + k*0x50)
        d = f.read(0x10)
        print("    k=%d (%-8s) %s" % (k, naam, " ".join("%08x" % x for x in struct.unpack("<4I", d))))
PY
}

echo "############ 0. vooraf"
date '+  starttijd: %H:%M:%S'; printf '  gamelog had %s regels\n' "$REGELS_VOOR"
pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game|wine|OpenParrotLoader' | grep -v pgrep | head -3 | sed 's/^/  draait al: /'
echo "    (leeg = vrij)"
grep -l '054c' /sys/class/input/event*/id/vendor 2>/dev/null | sed 's/^/  Move: /'; echo "  (leeg bij Move = niet aangesloten)"

echo
echo "############ 1. de run (180 s, beeld op de tv)"
python3 -c "open('$JVS','wb').write(bytes(64))"
cd "$R"
( timeout 320 scripts/play-native.sh --game Halo --gamescope --mute --seconds 180 \
    --bind-ro "$P/probe/libusb-tap.so" /rg/lib/libusb-tap.so \
    --bind-ro "$JVS" /rg/jvs --bind-rw "$TAPDIR" /rg/tap \
    --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
    --env RG_USB_LOG=/rg/tap/usb-tap.log --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1 \
    --env RG_RT_PULSE_MS=500 \
    > "$OUT/play.out" 2>&1; echo "$?" > "$OUT/exit" ) &
RUN=$!
sleep 12

echo "   --- FASE 1: START herhalen tot de ATTRACT-BANK in het logboek staat"
ATTRACT=0
for i in $(seq 1 20); do
  druk 64
  if grep -q 'sound bank: attract' "$G" 2>/dev/null; then
    ATTRACT=1
    echo "  *** de attract-bank staat erin na $i START-pulsen (t=$((SECONDS))s, $(date +%H:%M:%S))" | tee -a "$OUT/leven"
    break
  fi
  sleep 1
done
if [ "$ATTRACT" = 0 ]; then
  echo "  *** DE ATTRACT-BANK IS ER NIET GEKOMEN -- dat is op zich de uitkomst; ik stop hier" | tee -a "$OUT/leven"
  maak_foto "stop-geen-attract"
  tail -n +$((REGELS_VOOR+1)) "$G" | tail -6 | sed 's/^/    /' | tee -a "$OUT/leven"
  pkill -f '/pm/g7/halo/game' 2>/dev/null; pkill -f 'play-native.sh' 2>/dev/null
  cp -a "$G" "$P/gamelog-run15.txt" 2>/dev/null
  cp -a "$OUT/leven" "$P/run15-leven" 2>/dev/null
  for p in "$OUT"/*.png; do [ -f "$p" ] && cp -a "$p" "$P/run15-$(basename "$p")"; done
  echo "== gestopt"; exit 0
fi
sleep 5
PID=$(pgrep -x game 2>/dev/null | head -1)
maak_foto "00-bij-attract" | tee -a "$OUT/leven"
echo "  game-pid=${PID:-geen}; gamelog=$(wc -l < "$G") regels" | tee -a "$OUT/leven"
echo "  --- de tafel bij de attract-lus:"; tafel "$PID" | tee -a "$OUT/leven"

echo
echo "   --- FASE 2: SERVICE (bit 1 -> k=3), drie keer met een lange puls"
for i in 1 2 3; do
  echo "  t=$((SECONDS))s SERVICE-puls $i ($(date +%H:%M:%S))" | tee -a "$OUT/leven"
  druk 2
  sleep 5
  maak_foto "0$i-na-service$i" | tee -a "$OUT/leven"
  tafel "$PID" | tee -a "$OUT/leven"
done

echo
echo "   --- FASE 3: de volumetoetsen als menunavigatie (bit 4 = op, bit 5 = neer)"
for i in 1 2 3; do
  echo "  t=$((SECONDS))s volume-op $i (bit 4) ($(date +%H:%M:%S))" | tee -a "$OUT/leven"
  druk 16; sleep 4; maak_foto "1${i}a-na-volumeop" | tee -a "$OUT/leven"
  echo "  t=$((SECONDS))s volume-neer $i (bit 5)" | tee -a "$OUT/leven"
  druk 32; sleep 4; maak_foto "1${i}b-na-volumeneer" | tee -a "$OUT/leven"
done
maak_foto "99-eind" | tee -a "$OUT/leven"
echo "  gamelog aan het eind: $(wc -l < "$G") regels" | tee -a "$OUT/leven"
wait $RUN 2>/dev/null
echo "  exitcode: $(cat "$OUT/exit" 2>/dev/null)"

echo
echo "############ 2. WAT DE GAME SCHREEF (nieuwe regels, met tijdstempels)"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | tail -14 | sed 's/^/  /'

echo
echo "############ 3. DE IND RUKKEN (en welke bits langs kwamen)"
NAT=$(ls -t ~/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | head -1)
grep 'indruk bit' "$NAT" 2>/dev/null | tail -12 | sed 's/^/  /'
for b in 0 1 4 5 6; do printf '  bit %s: %s indrukken\n' "$b" "$(grep -c "indruk bit $b " "$NAT" 2>/dev/null || echo 0)"; done

echo
echo "############ 4. DE IDENTITEITSREGEL (eerste 6 regels)"
head -6 "$NAT" 2>/dev/null | sed 's/^/  /'

echo
echo "############ 5. opruimen (alleen mijn eigen pids) en de kern"
pkill -f '/pm/g7/halo/game' 2>/dev/null; pkill -f 'play-native.sh' 2>/dev/null; sleep 2
pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game' 2>/dev/null | grep -v pgrep | sed 's/^/  draait nog: /'; echo "  (leeg = schoon)"
sha256sum ~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/TeknoParrot/TeknoParrot64.dll 2>/dev/null | cut -c1-16 | sed 's/^/  kern: /'
cp -a "$G" "$P/gamelog-run15.txt" 2>/dev/null
cp -a "$OUT/leven" "$P/run15-leven" 2>/dev/null
cp -a "$NAT" "$P/run15-launcherlog.txt" 2>/dev/null
for p in "$OUT"/*.png; do [ -f "$p" ] && cp -a "$p" "$P/run15-$(basename "$p")"; done
ls -l "$P"/run15-*.png 2>/dev/null | sed 's/^/  /'
echo "  --- ruwe rommel: $OUT"
echo "== done"
