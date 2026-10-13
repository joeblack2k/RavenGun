#!/bin/bash
# RUN 14: HET SERVICEMENU. Alleen de SERVICE-schakelaar (bit 1 -> k=3), lange pulsen, foto per stap,
# en per stap de tafelmeting. GEEN TEST. (Eén START vooraf om de FileTest te passeren -- anders is er
# geen menu bereikbaar; dat meld ik als bewuste, minimale afwijking.)
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
D=~/.local/share/ravengun
TAPDIR="$D/tap"
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
JVS="$TAPDIR/start.jvs"
OUT=$(mktemp -d /tmp/run14-XXXX)
REGELS_VOOR=$(wc -l < "$G" 2>/dev/null)
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
TABEL=$((0x180ae80 + 0xaea0))
K3OFF=$((3*0x50))

maak_foto() { local doel="$OUT/$1.png"
  command -v spectacle >/dev/null && spectacle -b -n -o "$doel" >/dev/null 2>&1 && [ -s "$doel" ]; }
bit() { python3 -c "
import os, struct
fd = os.open('$JVS', os.O_WRONLY); os.pwrite(fd, struct.pack('<I', $1), 8); os.close(fd)"; }
start_puls() { bit 0; sleep 0.3; bit 64; sleep 0.6; bit 0; }
service()    { bit 0; sleep 0.3; bit 2;  sleep 0.8; bit 0; }   # bit 1 = service -> k=3, puls ~800 ms
tafel() { python3 - "$1" <<'PY'
import struct, sys
pid = int(sys.argv[1])
with open("/proc/%d/mem" % pid, "rb", 0) as f:
    for k, naam in ((3, "service"), (0, "start"), (4, "test")):
        f.seek(0x180ae80 + 0xaea0 + k*0x50)
        d = f.read(0x20)
        print("    bord 0 k=%d (%-7s): %s" % (k, naam, " ".join("%08x" % x for x in struct.unpack("<8I", d))))
PY
}

echo "############ 0. vooraf"
date '+  starttijd: %H:%M:%S'; printf '  gamelog had %s regels\n' "$REGELS_VOOR"
pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game|wine|OpenParrotLoader' | grep -v pgrep | head -3 | sed 's/^/  draait al: /'
echo "    (leeg = vrij; een wineserver alleen = residu)"
echo "  --- Move aangesloten? (moet nee zijn)"; grep -l '054c' /sys/class/input/event*/id/vendor 2>/dev/null | sed 's/^/    /'; echo "    (leeg = nee)"
echo "  --- DE MENUTOETSEN IN HET PROFIEL (staan er menu omhoog/omlaag in?):"
python3 -c "
import json
d = json.load(open('$R/packaging/tp-shm-profiles.json'))
p = (d.get('profiles') or d)['WartranTroopers']
for b in p['bits']:
    if any(w in b['signal'] for w in ('menu', 'up', 'down', 'service', 'test')):
        print('    %-12s bit %d' % (b['signal'], b['bit']))
" 2>/dev/null

echo
echo "############ 1. de run (200 s, beeld op de tv, RG_RT_PULSE_MS=800)"
python3 -c "open('$JVS','wb').write(bytes(64))"
cd "$R"
( timeout 360 scripts/play-native.sh --game Halo --gamescope --mute --seconds 200 \
    --bind-ro "$P/probe/libusb-tap.so" /rg/lib/libusb-tap.so \
    --bind-ro "$JVS" /rg/jvs --bind-rw "$TAPDIR" /rg/tap \
    --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
    --env RG_USB_LOG=/rg/tap/usb-tap.log --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1 \
    --env RG_RT_PULSE_MS=800 \
    > "$OUT/play.out" 2>&1; echo "$?" > "$OUT/exit" ) &
RUN=$!
sleep 12

echo "   --- de FileTest passeren (EEN START -- anders is er geen menu)"
for i in $(seq 1 20); do
  start_puls
  grep -q 'FileTest Reported' "$G" 2>/dev/null && { echo "  FileTest weg na $i pulsen (t=$((SECONDS))s)" | tee -a "$OUT/leven"; break; }
  sleep 1
done
sleep 25
PID=$(pgrep -x game 2>/dev/null | head -1)
maak_foto "00-voor-service" | tee -a "$OUT/leven"
echo "  game-pid=${PID:-geen}; gamelog=$(wc -l < "$G") regels" | tee -a "$OUT/leven"
echo "  --- de tafel VOOR de service-pulsen:"; tafel "$PID" | tee -a "$OUT/leven"

echo
echo "   --- SERVICE (bit 1 -> k=3), drie keer met een lange puls, foto per stap"
for i in 1 2 3; do
  echo "  t=$((SECONDS))s SERVICE-puls $i ($(date +%H:%M:%S))" | tee -a "$OUT/leven"
  service
  sleep 5
  maak_foto "0$i-na-service$i" | tee -a "$OUT/leven"
  echo "    gamelog=$(wc -l < "$G") regels" | tee -a "$OUT/leven"
  tafel "$PID" | tee -a "$OUT/leven"
done

echo
echo "   --- en nog een keer, met een langere tussenpoos, voor de zekerheid"
service; sleep 8; maak_foto "04-service-laatst" | tee -a "$OUT/leven"
sleep 5; maak_foto "05-eind" | tee -a "$OUT/leven"
echo "  gamelog aan het eind: $(wc -l < "$G") regels" | tee -a "$OUT/leven"
wait $RUN 2>/dev/null
echo "  exitcode: $(cat "$OUT/exit" 2>/dev/null)"

echo
echo "############ 2. WAT DE GAME SCHREEF (nieuwe regels)"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | tail -14 | sed 's/^/  /'

echo
echo "############ 3. WAT DE HULP MELDDE (en kwam bit 1 langs?)"
NAT=$(ls -t ~/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | head -1)
grep 'indruk bit' "$NAT" 2>/dev/null | sed 's/^/  /'
printf '  service-indrukken (bit 1): %s   start (bit 6): %s   test (bit 0): %s\n' \
  "$(grep -c 'indruk bit 1 ' "$NAT" 2>/dev/null || echo 0)" \
  "$(grep -c 'indruk bit 6 ' "$NAT" 2>/dev/null || echo 0)" \
  "$(grep -c 'indruk bit 0 ' "$NAT" 2>/dev/null || echo 0)"

echo
echo "############ 4. DE IDENTITEITSREGEL"
head -8 "$NAT" 2>/dev/null | sed 's/^/  /'

echo
echo "############ 5. opruimen (ALLEEN mijn eigen pids)"
pkill -f '/pm/g7/halo/game' 2>/dev/null; pkill -f 'play-native.sh' 2>/dev/null; sleep 2
pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game' 2>/dev/null | grep -v pgrep | sed 's/^/  draait nog: /'; echo "  (leeg = schoon)"
echo "  --- de runtime-kern (moet f5c7532d zijn):"
sha256sum ~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/TeknoParrot/TeknoParrot64.dll 2>/dev/null | cut -c1-16 | sed 's/^/    /'
cp -a "$G" "$P/gamelog-run14.txt" 2>/dev/null
cp -a "$OUT/leven" "$P/run14-leven" 2>/dev/null
cp -a "$NAT" "$P/run14-launcherlog.txt" 2>/dev/null
for p in "$OUT"/*.png; do [ -f "$p" ] && cp -a "$p" "$P/run14-$(basename "$p")"; done
ls -l "$P"/run14-*.png 2>/dev/null | sed 's/^/  /'
echo "  --- ruwe rommel: $OUT"
echo "== done"
