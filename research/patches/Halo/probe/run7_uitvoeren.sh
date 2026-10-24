#!/bin/bash
# RUN 7: START HERHALEN tot de FileTest eindigt, en pas DAN de TEST-pogingen op het kalibratiescherm.
#   1. START elke 5 s (puls 2000 ms) tot 'FileTest Reported' in het logboek staat
#   2. 25 s wachten zodat de webcam- en netwerkstap lopen
#   3. TEST k=3, k=4, k=5 met foto per poging, daarna herhaald TEST (k=4) elke 10 s
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
D=~/.local/share/ravengun
TAPDIR="$D/tap"
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
TAPLOG="$TAPDIR/usb-tap.log"
JVS="$TAPDIR/start.jvs"
PROBE="$TAPDIR/probe.txt"
OUT=$(mktemp -d /tmp/run7-XXXX)
REGELS_VOOR=$(wc -l < "$G" 2>/dev/null)
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
TABEL=$((0x180ae80 + 0xaea0))

maak_foto() { local doel="$OUT/$1.png"
  command -v spectacle >/dev/null && spectacle -b -n -o "$doel" >/dev/null 2>&1 && [ -s "$doel" ] && { echo "  foto $1"; return 0; }
  echo "  foto $1 MISLUKT"; }
bit() { python3 -c "
import os, struct
fd = os.open('$JVS', os.O_WRONLY); os.pwrite(fd, struct.pack('<I', $1), 8); os.close(fd)"; }
# een indruk = flank omhoog, puls van 2 s (de hulp houdt hem zolang vast), dan terug
druk() { bit 0; sleep 0.5; bit $1; sleep 2; bit 0; }

echo "############ 0. vooraf"
date '+  starttijd: %H:%M:%S'; printf '  gamelog had %s regels\n' "$REGELS_VOOR"
pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game' | grep -v pgrep | sed 's/^/  draait al: /'; echo "  (leeg = vrij)"

echo
echo "############ 1. de run (270 s, beeld op de tv)"
python3 -c "open('$JVS','wb').write(bytes(64))"
printf '0,0,4' > "$PROBE"
rm -f "$TAPLOG"; : > "$OUT/leven"
cd "$R"
( timeout 450 scripts/play-native.sh --game Halo --gamescope --mute --seconds 270 \
    --bind-ro "$P/probe/libusb-tap.so" /rg/lib/libusb-tap.so \
    --bind-ro "$JVS" /rg/jvs --bind-ro "$PROBE" /rg/tap/probe.txt --bind-rw "$TAPDIR" /rg/tap \
    --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
    --env RG_USB_LOG=/rg/tap/usb-tap.log --env RG_USB_RAW=1 --env RG_USB_MAX=50000 \
    --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1 --env RG_RT_PULSE_MS=2000 \
    --env RG_RT_PROBE=/rg/tap/probe.txt \
    > "$OUT/play.out" 2>&1; echo "$?" > "$OUT/exit" ) &
RUN=$!
sleep 12
PID=$(pgrep -x game 2>/dev/null | head -1); echo "  game-pid=${PID:-geen} op t=12s" | tee -a "$OUT/leven"
# de buitenlezer (dezelfde adressen als de hulp)
( P="$PID"; [ -n "$P" ] || exit 0
  vorige=""
  for s in $(seq 1 130); do
    regel=""
    for k in 3 4 5; do
      dd if=/proc/$P/mem bs=1 skip=$((TABEL + k*0x50)) count=16 of="$OUT/v$k" 2>/dev/null
      regel="$regel k$k=$(xxd -p "$OUT/v$k" 2>/dev/null | head -c 32)"
    done
    [ "$regel" != "$vorige" ] && { echo "BUITEN t=$((s*2))s $regel" >> "$OUT/tabel.txt"; vorige="$regel"; }
    sleep 2
  done ) &
LEZER=$!

echo
echo "   --- FASE 1: START herhalen tot de FileTest eindigt"
for i in $(seq 1 12); do
  date "+  t=$((SECONDS))s START-puls $i  %H:%M:%S" | tee -a "$OUT/leven"
  druk 64
  if grep -q 'FileTest Reported' "$G" 2>/dev/null; then
    echo "  *** de FileTest is na $i pulsen geeindigd" | tee -a "$OUT/leven"; break
  fi
  sleep 1
done
grep -q 'FileTest Reported' "$G" 2>/dev/null && echo "  FileTest Reported staat in het logboek" | tee -a "$OUT/leven" || echo "  LET OP: FileTest Reported staat er NIET" | tee -a "$OUT/leven"
maak_foto "na-start" | tee -a "$OUT/leven"
echo "  gamelog: $(wc -l < "$G") regels" | tee -a "$OUT/leven"

echo
echo "   --- FASE 2: wachten op de webcam- en netwerkstap (25 s)"
sleep 25
echo "  gamelog: $(wc -l < "$G") regels" | tee -a "$OUT/leven"
maak_foto "voor-test" | tee -a "$OUT/leven"

echo
echo "   --- FASE 3: TEST op k=3, k=4, k=5 (foto per poging)"
for K in 3 4 5; do
  printf '%s' "0,0,$K" > "$PROBE"; sleep 1
  date "+  t=$((SECONDS))s TEST k=$K  %H:%M:%S" | tee -a "$OUT/leven"
  druk 1
  sleep 12
  maak_foto "test-k$K" | tee -a "$OUT/leven"
  echo "    gamelog na k=$K: $(wc -l < "$G") regels" | tee -a "$OUT/leven"
done

echo
echo "   --- FASE 4: TEST herhalen (k=4, elke 10 s, 6 keer)"
printf '0,0,4' > "$PROBE"
for i in $(seq 1 6); do
  date "+  t=$((SECONDS))s TEST-herhaling $i  %H:%M:%S" | tee -a "$OUT/leven"
  druk 1; sleep 8
done
maak_foto "eind" | tee -a "$OUT/leven"
echo "  gamelog aan het eind: $(wc -l < "$G") regels" | tee -a "$OUT/leven"
kill $LEZER 2>/dev/null
wait $RUN 2>/dev/null
echo "  exitcode: $(cat "$OUT/exit" 2>/dev/null)"

echo
echo "############ 2. WAT DE GAME SCHREEF (nieuwe regels, met tijdstempels)"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | sed 's/^/  /'

echo
echo "############ 3. de buitenlezer (tabelwaarden op dezelfde adressen)"
[ -f "$OUT/tabel.txt" ] && grep -E 'k3|k4|k5' "$OUT/tabel.txt" | head -14 | sed 's/^/  /' || echo "  (niets gezien)"

echo
echo "############ 4. wat de hulp meldde (indrukken en tafelwaarden), laatste 12"
NAT=$(ls -t ~/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | head -1)
grep -E 'indruk bit|tabel bord' "$NAT" 2>/dev/null | tail -12 | sed 's/^/  /'
echo "  --- aantal indrukken: $(grep -c 'indruk bit' "$NAT" 2>/dev/null || echo 0)"

echo
echo "############ 5. crash?"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | grep -c 'FATAL signal' | sed 's/^/  FATAL-regels: /'
echo "  --- en de tap:"
[ -f "$TAPLOG" ] && for s in 'tap start' 'attr idVendor' 'APPARAAT' 'descriptors'; do printf '    %-16s %s\n' "$s" "$(grep -c "$s" "$TAPLOG")"; done

echo
echo "############ 6. opruimen"
pkill -f '/pm/g7/halo/game' 2>/dev/null; pkill -f 'play-native.sh' 2>/dev/null; kill $LEZER 2>/dev/null
sleep 2
pgrep -a -f 'gamescope|wineserver|play-native|/pm/g7/halo/game' 2>/dev/null | grep -v pgrep | sed 's/^/  draait nog: /'; echo "  (leeg = schoon)"
cp -a "$G" "$P/gamelog-run7.txt" 2>/dev/null
[ -f "$TAPLOG" ] && cp -a "$TAPLOG" "$P/usb-tap-run7.log"
for p in "$OUT"/*.png; do [ -f "$p" ] && cp -a "$p" "$P/run7-$(basename "$p")"; done
ls -l "$P"/run7-*.png 2>/dev/null | sed 's/^/  /'
echo "  --- ruwe rommel: $OUT"
echo "== done"
