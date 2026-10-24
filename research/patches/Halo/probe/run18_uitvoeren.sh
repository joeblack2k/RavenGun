#!/bin/bash
# RUN 18 = PROEF 1: KOPPEL EERST EEN BRON (muis) AAN SPELER 1, dan pas arm + test-pattern.
# Vraag: bewegen de tp-shm-aimbytes (12/13) DAN wel? Zo nee, dan moet de daemon worden uitgebreid.
# Voorspelling vooraf: MET een gekoppelde bron beweegt shm[12]/[13]; zo niet, dan is dat de uitkomst.
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
D=~/.local/share/ravengun
TAPDIR="$D/tap"
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
JVS="$TAPDIR/start.jvs"
TABELK="$P/rt-invoer.kandidaat.ini"
OUT=$(mktemp -d /tmp/run18-XXXX)
REGELS_VOOR=$(wc -l < "$G" 2>/dev/null)
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
NIEUW() { tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null; }
RAV=~/projects/ravengun/build/ravengunctl

maak_foto() { local doel="$OUT/$1.png"
  command -v spectacle >/dev/null && spectacle -b -n -o "$doel" >/dev/null 2>&1 && [ -s "$doel" ]; }
bit() { python3 -c "
import os, struct
fd = os.open('$JVS', os.O_WRONLY); os.pwrite(fd, struct.pack('<I', $1), 8); os.close(fd)"; }
druk() { bit 0; sleep 0.3; bit $1; sleep 0.5; bit 0; }
bron() { python3 -c "
d=open('$JVS','rb').read(64); print('      shm[8..11]=%d,%d,%d,%d  shm[12]=%d shm[13]=%d shm[14]=%d shm[15]=%d' % (d[8],d[9],d[10],d[11],d[12],d[13],d[14],d[15]))"; }
spel() { python3 - "$1" <<'PY'
import struct, sys
pid=int(sys.argv[1])
with open("/proc/%d/mem"%pid,"rb",0) as f:
    for naam,adr in (("aim x (0x1f392a0)",0x1f392a0),("aim y (0x1f392a4)",0x1f392a4),
                     ("blok+0",0x1a7cb20),("blok+0x3c",0x1a7cb5c),("spelersaantal [0x1f392d0]",0x1f392d0)):
        f.seek(adr); d=f.read(4); print("      %-26s u32=%-12d f32=%.6g" % (naam,struct.unpack("<I",d)[0],struct.unpack("<f",d)[0]))
PY
}
stop_en_meld() { echo "  *** $1" | tee -a "$OUT/leven"; maak_foto "stop"
  pkill -f '/pm/g7/halo/game' 2>/dev/null; pkill -f 'play-native.sh' 2>/dev/null
  cp -a "$G" "$P/gamelog-run18.txt" 2>/dev/null; cp -a "$OUT/leven" "$P/run18-leven" 2>/dev/null
  for p in "$OUT"/*.png; do [ -f "$p" ] && cp -a "$p" "$P/run18-$(basename "$p")"; done
  echo "== gestopt"; exit 0; }

echo "############ 0. VOORAF"
date '+  starttijd: %H:%M:%S'
sha256sum "$R/build/librg_rt_input.so" | sed 's/^/  artefact /'
sha256sum "$TABELK" | sed 's/^/  tabel   /'
pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game|OpenParrotLoader|amdaemon' | grep -v pgrep | head -3 | sed 's/^/  bezet: /'; echo "  (leeg = vrij)"
grep -l '054c' /sys/class/input/event*/id/vendor 2>/dev/null | sed 's/^/  Move: /'; echo "  (leeg = niet aangesloten)"
echo "  --- WAT DE DAEMON ZEGT VOOR HET WAPENEN (rappoorten en de bestaande koppeling):"
$RAV status 2>&1 | grep -iE 'Move|Rapporten|Verbinding|Uitvoer' | head -5 | sed 's/^/    /'
echo "  --- en de muizen die ik kan koppelen:"
$RAV mice 2>&1 | head -6 | sed 's/^/    /'

echo
echo "############ 1. DE RUN (150 s, beeld op de tv)"
python3 -c "open('$JVS','wb').write(bytes(64))"
cd "$R"
( timeout 300 scripts/play-native.sh --game Halo --gamescope --mute --seconds 150 \
    --bind-ro "$P/probe/libusb-tap.so" /rg/lib/libusb-tap.so \
    --bind-ro "$JVS" /rg/jvs --bind-rw "$TAPDIR" /rg/tap \
    --bind-ro "$TABELK" /rg/tabel/rt-invoer.ini \
    --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
    --env RG_USB_LOG=/rg/tap/usb-tap.log --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1 \
    --env RG_RT_PULSE_MS=500 --env RG_RT_TABLE=/rg/tabel/rt-invoer.ini \
    > "$OUT/play.out" 2>&1; echo "$?" > "$OUT/exit" ) &
RUN=$!
sleep 12
NAT=$(ls -t ~/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | head -1)
grep -iE 'tabel |FOUT' "$NAT" 2>/dev/null | head -2 | sed 's/^/  hulp: /'

echo "   --- attract-lus halen (START, nieuwe regels tellen)"
T0=$SECONDS; A=0
for i in $(seq 1 30); do
  druk 64
  NIEUW | grep -q 'sound bank: attract' && { A=1; echo "  *** attract na $i pulsen (t=$((SECONDS))s, $(date +%H:%M:%S))" | tee -a "$OUT/leven"; break; }
  [ $((SECONDS-T0)) -gt 60 ] && break
  sleep 2
done
[ "$A" = 0 ] && stop_en_meld "geen attract-regel binnen 60 s -- dat is de uitkomst"
sleep 3
PID=$(pgrep -x game | head -1)
maak_foto "01-bij-attract"

echo
echo "   --- A. DE BRON VOOR HET KOPPELEN (moet 0/0 zijn, zoals in run 17)"
bron | tee -a "$OUT/leven"; spel "$PID" | head -2 | tee -a "$OUT/leven"

echo
echo "   --- B. KOPPELEN: mouse-gun --player 1 --mouse <eerste muis uit de lijst>"
MUIS=$($RAV mice 2>/dev/null | sed -n '1p' | awk '{print $1}')
echo "      gekozen muis-id: ${MUIS:-GEEN}" | tee -a "$OUT/leven"
$RAV mouse-gun --player 1 --mouse "${MUIS:-}" 2>&1 | head -4 | sed 's/^/      /' | tee -a "$OUT/leven"
echo "      koppeling in de status:"; $RAV status 2>&1 | grep -iE 'muis|gun|speler|koppel|Uitvoer' | head -4 | sed 's/^/        /' | tee -a "$OUT/leven"

echo
echo "   --- C. WAPENEN en de bron opnieuw lezen"
$RAV arm 2>&1 | head -2 | sed 's/^/      /' | tee -a "$OUT/leven"
sleep 2; echo "      na het wapenen:"; bron | tee -a "$OUT/leven"

echo
echo "   --- D. DE POSITIE STUREN (dit is de vraag: komt het in de shm?)"
for xy in "0.5 0.5" "0.2 0.2" "0.8 0.8"; do
  echo "  t=$((SECONDS))s test-pattern point $xy" | tee -a "$OUT/leven"
  $RAV test-pattern point $xy 2>&1 | grep -iE 'Positie|aim' | head -2 | sed 's/^/      /' | tee -a "$OUT/leven"
  sleep 3
  bron | tee -a "$OUT/leven"
  spel "$PID" | head -2 | tee -a "$OUT/leven"
  maak_foto "aim-$(echo $xy | tr ' .' '__')"
done

echo
echo "   --- E. en nog een keer, met een korte wachttijd erna (blijft het staan?)"
$RAV test-pattern point 0.35 0.65 2>&1 | grep -iE 'Positie' | head -1 | sed 's/^/      /'
sleep 5; bron | tee -a "$OUT/leven"; spel "$PID" | head -2 | tee -a "$OUT/leven"
maak_foto "99-eind"
wait $RUN 2>/dev/null
echo "  exitcode: $(cat "$OUT/exit" 2>/dev/null)"

echo
echo "############ 2. DE NIEUWE LOGBOEKREGELS"
NIEUW | tail -8 | sed 's/^/  /'

echo
echo "############ 3. opruimen (alleen mijn eigen pids)"
$RAV mouse-gun --player 1 --off 2>&1 | head -2 | sed 's/^/  /'
$RAV disarm 2>&1 | head -1 | sed 's/^/  /'
pkill -f '/pm/g7/halo/game' 2>/dev/null; pkill -f 'play-native.sh' 2>/dev/null; sleep 2
pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game' | grep -v pgrep | sed 's/^/  draait nog: /'; echo "  (leeg = schoon)"
cp -a "$G" "$P/gamelog-run18.txt" 2>/dev/null; cp -a "$OUT/leven" "$P/run18-leven" 2>/dev/null
[ -n "${NAT:-}" ] && cp -a "$NAT" "$P/run18-launcherlog.txt" 2>/dev/null
for p in "$OUT"/*.png; do [ -f "$p" ] && cp -a "$p" "$P/run18-$(basename "$p")"; done
ls -1 "$P"/run18-*.png 2>/dev/null | sed 's/^/  /'
echo "== done"
