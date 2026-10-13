#!/bin/bash
# RUN 8: het kalibratiescherm bedienen via de MUISMODUS en de uitvoer van het virtuele lichtgeweer.
#   - eerst: welke launcher (pad + sha256), mice, status, en ARM
#   - run zoals run 7 (START herhalen tot de FileTest eindigt) -> kalibratiescherm
#   - dan per stap: test-pattern point X Y + output-button trigger tap, foto per stap
#   - en een lezer op /dev/input/event13 die vastlegt wat het lichtgeweer UITZOND
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
D=~/.local/share/ravengun
CTL="$R/build/ravengunctl"
TAPDIR="$D/tap"
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
TAPLOG="$TAPDIR/usb-tap.log"
JVS="$TAPDIR/start.jvs"
PROBE="$TAPDIR/probe.txt"
OUT=$(mktemp -d /tmp/run8-XXXX)
REGELS_VOOR=$(wc -l < "$G" 2>/dev/null)
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"

maak_foto() { local doel="$OUT/$1.png"
  command -v spectacle >/dev/null && spectacle -b -n -o "$doel" >/dev/null 2>&1 && [ -s "$doel" ] && { echo "  foto $1"; return 0; }
  echo "  foto $1 MISLUKT"; }
bit() { python3 -c "
import os, struct
fd = os.open('$JVS', os.O_WRONLY); os.pwrite(fd, struct.pack('<I', $1), 8); os.close(fd)"; }
druk() { bit 0; sleep 0.5; bit $1; sleep 2; bit 0; }

echo "############ 0. WELKE LAUNCHER, EN WAT ZEGT HET GEREEDSCHAP VOORAF"
echo "  play-native.sh : $R/scripts/play-native.sh"
sha256sum "$R/scripts/play-native.sh" | cut -c1-16 | sed 's/^/    sha256 /'
echo "  launch-direct.py: $R/scripts/launch-direct.py"
sha256sum "$R/scripts/launch-direct.py" | cut -c1-16 | sed 's/^/    sha256 /'
echo "  (NIET de geinstalleerde launcher in ~/.local/opt/ravengun; en de --exe-override in de werkboom"
echo "   gebruik ik niet -- die laat ik met rust.)"
echo "  --- git-status van die twee bestanden (zodat duidelijk is wat ik gebruikte):"
( cd "$R" && git status --porcelain scripts/play-native.sh scripts/launch-direct.py 2>/dev/null | sed 's/^/    /' )
echo
echo "  --- ravengunctl: $CTL"
sha256sum "$CTL" 2>/dev/null | cut -c1-16 | sed 's/^/    sha256 /'
echo "  --- mice VOOR:"
"$CTL" mice 2>&1 | sed -n '/Toewijzingen/,$p' | head -8 | sed 's/^/    /'
echo "  --- status VOOR (de relevante blokken):"
"$CTL" status 2>&1 | grep -iE '^Daemon|^Move|^Verbinding|^Knoppen|^IO-brug' | sed 's/^/    /'

echo
echo "############ 1. ARM (zonder dit gaat er niets naar het virtuele lichtgeweer)"
"$CTL" arm 2>&1 | head -5 | sed 's/^/  /'
sleep 1
"$CTL" output 2>&1 | head -12 | sed 's/^/  /'

echo
echo "############ 2. de lezer op het virtuele lichtgeweer (event13)"
python3 - "$OUT/gun.txt" <<'PY' &
import struct, sys, time
pad = sys.argv[1]
f = open(pad, "w", buffering=1)
ev = open("/dev/input/event13", "rb", buffering=0)
f.write("lezer gestart %.3f\n" % time.time())
while True:
    d = ev.read(24)
    if len(d) < 24:
        break
    s, us, typ, code, val = struct.unpack("llHHi", d)
    if typ == 3 and code in (0, 1):
        f.write("%.3f ABS_%s=%d\n" % (time.time(), "X" if code == 0 else "Y", val))
    elif typ == 1 and code == 272:
        f.write("%.3f BTN_LEFT=%d\n" % (time.time(), val))
PY
LEZER=$!
sleep 1

echo
echo "############ 3. de run (300 s, beeld op de tv)"
python3 -c "open('$JVS','wb').write(bytes(64))"; printf '0,0,4' > "$PROBE"
rm -f "$TAPLOG"; : > "$OUT/leven"
cd "$R"
( timeout 480 scripts/play-native.sh --game Halo --gamescope --mute --seconds 300 \
    --bind-ro "$P/probe/libusb-tap.so" /rg/lib/libusb-tap.so \
    --bind-ro "$JVS" /rg/jvs --bind-ro "$PROBE" /rg/tap/probe.txt --bind-rw "$TAPDIR" /rg/tap \
    --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
    --env RG_USB_LOG=/rg/tap/usb-tap.log --env RG_USB_RAW=1 --env RG_USB_MAX=50000 \
    --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1 --env RG_RT_PULSE_MS=2000 \
    --env RG_RT_PROBE=/rg/tap/probe.txt \
    > "$OUT/play.out" 2>&1; echo "$?" > "$OUT/exit" ) &
RUN=$!
sleep 12
echo "   --- START herhalen tot de FileTest eindigt"
for i in $(seq 1 12); do
  echo "  t=$((SECONDS))s START-puls $i" | tee -a "$OUT/leven"
  druk 64
  grep -q 'FileTest Reported' "$G" 2>/dev/null && { echo "  *** FileTest geeindigd na puls $i" | tee -a "$OUT/leven"; break; }
  sleep 1
done
grep -q 'FileTest Reported' "$G" && echo "  FileTest Reported staat erin" | tee -a "$OUT/leven" || echo "  LET OP: geen FileTest Reported" | tee -a "$OUT/leven"
sleep 22
maak_foto "01-kalibratie" | tee -a "$OUT/leven"
echo "  gamelog: $(wc -l < "$G") regels" | tee -a "$OUT/leven"

echo
echo "   --- STAP 1: de loop naar LINKSBOVEN (test-pattern point 0 0)"
date '+  %H:%M:%S' | tee -a "$OUT/leven"
"$CTL" test-pattern point 0 0 2>&1 | head -3 | sed 's/^/    /' | tee -a "$OUT/leven"
sleep 4
maak_foto "02-linksboven" | tee -a "$OUT/leven"

echo
echo "   --- STAP 2: de TREKKER (output-button trigger tap)"
date '+  %H:%M:%S' | tee -a "$OUT/leven"
"$CTL" output-button trigger tap 2>&1 | head -3 | sed 's/^/    /' | tee -a "$OUT/leven"
sleep 5
maak_foto "03-na-trekker" | tee -a "$OUT/leven"
echo "    gamelog: $(wc -l < "$G") regels" | tee -a "$OUT/leven"

echo
echo "   --- STAP 3: trekker nog twee keer (met tussenpozen)"
for i in 2 3; do
  date "+  %H:%M:%S  trekker $i" | tee -a "$OUT/leven"
  "$CTL" test-pattern point 0 0 >/dev/null 2>&1
  sleep 2
  "$CTL" output-button trigger tap >/dev/null 2>&1
  sleep 4
  maak_foto "0$((2+i))-trekker$i" | tee -a "$OUT/leven"
  echo "    gamelog: $(wc -l < "$G") regels" | tee -a "$OUT/leven"
done

echo
echo "   --- STAP 4: ook het midden en de hoek rechtsonder, met trekker"
for POS in "0.5 0.5" "1 1"; do
  date "+  %H:%M:%S  positie $POS + trekker" | tee -a "$OUT/leven"
  "$CTL" test-pattern point $POS >/dev/null 2>&1; sleep 2
  "$CTL" output-button trigger tap >/dev/null 2>&1; sleep 4
done
maak_foto "06-eind" | tee -a "$OUT/leven"
echo "  gamelog aan het eind: $(wc -l < "$G") regels" | tee -a "$OUT/leven"
kill $LEZER 2>/dev/null
wait $RUN 2>/dev/null
echo "  exitcode: $(cat "$OUT/exit" 2>/dev/null)"

echo
echo "############ 4. WAT HET LICHTGEWEER UITZOND (de meting)"
if [ -f "$OUT/gun.txt" ]; then
  echo "  regels: $(wc -l < "$OUT/gun.txt")"
  echo "  --- ABS_X/ABS_Y-bereik en de trekker:"
  grep -c 'ABS_X' "$OUT/gun.txt" | sed 's/^/    ABS_X-events: /'
  grep -oE 'ABS_X=[0-9]+' "$OUT/gun.txt" | sort -t= -k2 -n | sed -n '1p;$p' | sed 's/^/    /'
  grep -oE 'ABS_Y=[0-9]+' "$OUT/gun.txt" | sort -t= -k2 -n | sed -n '1p;$p' | sed 's/^/    /'
  grep 'BTN_LEFT' "$OUT/gun.txt" | head -12 | sed 's/^/    /'
  echo "    aantal BTN_LEFT-events: $(grep -c 'BTN_LEFT' "$OUT/gun.txt")"
else
  echo "  *** geen lezerbestand"
fi

echo
echo "############ 5. WAT DE GAME SCHREEF (nieuwe regels)"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | tail -10 | sed 's/^/  /'
echo "  --- is dit de eerste keer dat de game verder kwam dan de netwerkstap?"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | grep -cE 'attract|level1' | sed 's/^/    regels met attract/level1: /'
echo "  --- crash?"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | grep -c 'FATAL signal' | sed 's/^/    FATAL-regels: /'

echo
echo "############ 6. opruimen"
pkill -f '/pm/g7/halo/game' 2>/dev/null; pkill -f 'play-native.sh' 2>/dev/null; kill $LEZER 2>/dev/null
sleep 2
"$CTL" test-pattern stop >/dev/null 2>&1
"$CTL" disarm >/dev/null 2>&1 && echo "  het virtuele lichtgeweer is gedisarmeerd" || echo "  disarm gaf geen antwoord"
pgrep -a -f 'gamescope|wineserver|play-native|/pm/g7/halo/game' 2>/dev/null | grep -v pgrep | sed 's/^/  draait nog: /'; echo "  (leeg = schoon)"
cp -a "$G" "$P/gamelog-run8.txt" 2>/dev/null
[ -f "$TAPLOG" ] && cp -a "$TAPLOG" "$P/usb-tap-run8.log"
for p in "$OUT"/*.png; do [ -f "$p" ] && cp -a "$p" "$P/run8-$(basename "$p")"; done
[ -f "$OUT/gun.txt" ] && cp -a "$OUT/gun.txt" "$P/run8-lichtgeweer.txt"
ls -l "$P"/run8-*.png 2>/dev/null | sed 's/^/  /'
echo "  --- ruwe rommel: $OUT"
echo "== done"
