#!/bin/bash
# RUN 16 (KLAAR OM TE DRAAIEN): eerst naar de attract-lus met ALLEEN DE NIEUWE LOGBOEKREGELS als maatstaf,
# dan pas service, dan de volumetoetsen als menunavigatie.
# De correctie van run 15 zit in twee regels: NIEUW=tail -n +$((REGELS_VOOR+1)) en daarop alle controles.
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
D=~/.local/share/ravengun
TAPDIR="$D/tap"
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
JVS="$TAPDIR/start.jvs"
OUT=$(mktemp -d /tmp/run16-XXXX)
REGELS_VOOR=$(wc -l < "$G" 2>/dev/null)
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
NIEUW() { tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null; }

maak_foto() { local doel="$OUT/$1.png"
  command -v spectacle >/dev/null && spectacle -b -n -o "$doel" >/dev/null 2>&1 && [ -s "$doel" ]; }
bit() { python3 -c "
import os, struct
fd = os.open('$JVS', os.O_WRONLY); os.pwrite(fd, struct.pack('<I', $1), 8); os.close(fd)"; }
druk() { bit 0; sleep 0.3; bit $1; sleep 0.7; bit 0; }   # puls ~700 ms (RG_RT_PULSE_MS=500 begrenst)
tafel() { python3 - "$1" <<'PY'
import struct, sys
pid = int(sys.argv[1])
with open("/proc/%d/mem" % pid, "rb", 0) as f:
    for k, naam in ((3, "service"), (0, "start"), (4, "test"), (1, "volume+"), (2, "volume-")):
        f.seek(0x180ae80 + 0xaea0 + k*0x50)
        print("    k=%d (%-8s) %s" % (k, naam, " ".join("%08x" % x for x in struct.unpack("<4I", f.read(0x10)))))
PY
}
stop_en_meld() {   # $1 = reden
  echo "  *** $1" | tee -a "$OUT/leven"
  maak_foto "stop"
  echo "  --- de nieuwe regels van deze run:"; NIEUW | tail -8 | sed 's/^/    /' | tee -a "$OUT/leven"
  pkill -f '/pm/g7/halo/game' 2>/dev/null; pkill -f 'play-native.sh' 2>/dev/null
  cp -a "$G" "$P/gamelog-run16.txt" 2>/dev/null; cp -a "$OUT/leven" "$P/run16-leven" 2>/dev/null
  for p in "$OUT"/*.png; do [ -f "$p" ] && cp -a "$p" "$P/run16-$(basename "$p")"; done
  echo "== gestopt"; exit 0
}

echo "############ 0. vooraf (huisregel: ook de WINE-kant moet vrij zijn)"
date '+  starttijd: %H:%M:%S'; printf '  gamelog had %s regels (alleen de regels DAARNA tellen mee)\n' "$REGELS_VOOR"
BEZET=$(pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game|OpenParrotLoader|Transformers|Shell\.exe|Scarlet|VACUUM' 2>/dev/null | grep -v pgrep | head -5)
echo "$BEZET" | sed 's/^/  bezet: /'
if [ -n "$BEZET" ]; then echo "  *** de bench is bezet; ik start niets"; exit 0; fi
WS=$(pgrep -x wineserver 2>/dev/null | wc -l)
printf '  wineserver-processen: %s (alleen residu is ok, een lader/spel niet)\n' "$WS"
grep -l '054c' /sys/class/input/event*/id/vendor 2>/dev/null | sed 's/^/  Move: /'; echo "  (leeg = niet aangesloten, zoals bedoeld)"

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

echo "   --- FASE 1: de FileTest passeren (START)"
for i in $(seq 1 20); do
  druk 64
  NIEUW | grep -q 'FileTest Reported' && { echo "  FileTest weg na $i pulsen (t=$((SECONDS))s)" | tee -a "$OUT/leven"; break; }
  sleep 1
done

echo "   --- FASE 1b: START blijven herhalen tot de NIEUWE attract-regel er staat (max ~60 s)"
T0=$SECONDS; ATTRACT=0
for i in $(seq 1 20); do
  druk 64
  if NIEUW | grep -q 'sound bank: attract'; then
    ATTRACT=1
    echo "  *** NIEUWE attract-regel na $i pulsen (t=$((SECONDS))s, $(date +%H:%M:%S))" | tee -a "$OUT/leven"
    break
  fi
  [ $((SECONDS-T0)) -gt 60 ] && break
  sleep 2
done
if [ "$ATTRACT" = 0 ]; then
  stop_en_meld "DE ATTRACT-LUS IS NIET GEKOMEN binnen 60 s na de FileTest -- dat is op zich de uitkomst"
fi
sleep 4
PID=$(pgrep -x game 2>/dev/null | head -1)
maak_foto "00-bij-attract" | tee -a "$OUT/leven"
echo "  game-pid=${PID:-geen}; nieuwe regels: $(NIEUW | wc -l)" | tee -a "$OUT/leven"
echo "  --- de tafel BIJ de attract-lus:"; tafel "$PID" | tee -a "$OUT/leven"

echo
echo "   --- FASE 2: SERVICE (bit 1 -> k=3), drie keer"
for i in 1 2 3; do
  echo "  t=$((SECONDS))s SERVICE-puls $i ($(date +%H:%M:%S))" | tee -a "$OUT/leven"
  druk 2; sleep 5
  maak_foto "0$i-na-service$i" | tee -a "$OUT/leven"
  tafel "$PID" | tee -a "$OUT/leven"
  echo "    nieuwe regels: $(NIEUW | wc -l)" | tee -a "$OUT/leven"
done

echo
echo "   --- FASE 3: volumetoetsen als menunavigatie (bit 4 = op, bit 5 = neer)"
for i in 1 2 3; do
  druk 16; sleep 4; maak_foto "1${i}a-volumeop" | tee -a "$OUT/leven"
  druk 32; sleep 4; maak_foto "1${i}b-volumeneer" | tee -a "$OUT/leven"
  echo "    t=$((SECONDS))s nieuwe regels: $(NIEUW | wc -l)" | tee -a "$OUT/leven"
done
maak_foto "99-eind" | tee -a "$OUT/leven"
wait $RUN 2>/dev/null
echo "  exitcode: $(cat "$OUT/exit" 2>/dev/null)"

echo
echo "############ 2. DE NIEUWE REGELS VAN DEZE RUN (met tijdstempels)"
NIEUW | sed 's/^/  /'

echo
echo "############ 3. DE INDRUKKEN"
NAT=$(ls -t ~/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | head -1)
grep 'indruk bit' "$NAT" 2>/dev/null | tail -14 | sed 's/^/  /'
for b in 0 1 4 5 6; do printf '  bit %s: %s\n' "$b" "$(grep -c "indruk bit $b " "$NAT" 2>/dev/null || echo 0)"; done

echo
echo "############ 4. DE IDENTITEITSREGEL"
head -6 "$NAT" 2>/dev/null | sed 's/^/  /'

echo
echo "############ 5. opruimen (alleen mijn eigen pids) + de kern"
pkill -f '/pm/g7/halo/game' 2>/dev/null; pkill -f 'play-native.sh' 2>/dev/null; sleep 2
pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game' 2>/dev/null | grep -v pgrep | sed 's/^/  draait nog: /'; echo "  (leeg = schoon)"
sha256sum ~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/TeknoParrot/TeknoParrot64.dll 2>/dev/null | cut -c1-16 | sed 's/^/  kern: /'
cp -a "$G" "$P/gamelog-run16.txt" 2>/dev/null; cp -a "$OUT/leven" "$P/run16-leven" 2>/dev/null
cp -a "$NAT" "$P/run16-launcherlog.txt" 2>/dev/null
for p in "$OUT"/*.png; do [ -f "$p" ] && cp -a "$p" "$P/run16-$(basename "$p")"; done
echo "  --- ruwe rommel: $OUT"
echo "== done"
