#!/bin/bash
# RUN 6: de drie vragen in een run.
#   0. wat voor invoer is er nu? (bench-vrij, eerst)
#   1. de hulp logt de TAFELWAARDEN die de game leest, rond elke indruk (meting i.p.v. aanname)
#   2. POGING 1 (index): TEST op bord 0 met k=3, k=4, k=5, 15 s ertussen, foto per poging
#   3. POGING 2 (moment): TEST elke 10 s, ~90 s lang, met een foto aan het eind
#   En van BUITEN het proces lees ik dezelfde tabeladressen mee via /proc/<pid>/mem.
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
D=~/.local/share/ravengun
S="$R/tools/rt-input-shim/rg_rt_input.c"
SO="$R/build/librg_rt_input.so"
TAPDIR="$D/tap"
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
TAPLOG="$TAPDIR/usb-tap.log"
JVS="$TAPDIR/start.jvs"
PROBE="$TAPDIR/probe.txt"
OUT=$(mktemp -d /tmp/run6-XXXX)
REGELS_VOOR=$(wc -l < "$G" 2>/dev/null)
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
# de adressen die de hulp gebruikt (uit de bron): tabel + bord*0x2200 + k*0x50
TABEL=$((0x180ae80 + 0xaea0))

maak_foto() { local doel="$OUT/$1.png"
  command -v spectacle >/dev/null && spectacle -b -n -o "$doel" >/dev/null 2>&1 && [ -s "$doel" ] && { echo "  foto $1"; return 0; }
  echo "  foto $1 MISLUKT"; }
bit() { python3 -c "
import os, struct
fd = os.open('$JVS', os.O_WRONLY); os.pwrite(fd, struct.pack('<I', $1), 8); os.close(fd)"; }
puls() { bit 0; sleep 1; bit $1; }     # $1 = de bitwaarde (1 = test)

echo "############ 0. WAT VOOR INVOER IS ER NU?"
printf '  %-20s %-30s %s\n' "node" "naam" "VID:PID"
for e in /sys/class/input/event*; do
  [ -e "$e" ] || continue
  printf '  %-20s %-30s %s:%s\n' "/dev/input/$(basename "$e")" \
    "$(cat "$e/device/name" 2>/dev/null | tr -d '\n' | cut -c1-30)" \
    "$(cat "$e/device/id/vendor" 2>/dev/null)" "$(cat "$e/device/id/product" 2>/dev/null)"
done
echo "  --- Sony (054c) ergens?"
grep -l '054c' /sys/class/input/event*/device/id/vendor 2>/dev/null | sed 's/^/    /'
echo "  (leeg = geen Sony-apparaat aangesloten)"
echo "  --- hidraw:"
for h in /sys/class/hidraw/hidraw*; do [ -e "$h" ] && printf '    %s %s\n' "$(basename "$h")" "$(grep -h HID_NAME "$h/device/uevent" 2>/dev/null | cut -d= -f2-)"; done

echo
echo "############ 1. de hulp laat de TAFELWAARDEN zien (patchen + bouwen)"
python3 - "$S" <<'PY'
import io, sys
p = sys.argv[1]
s = io.open(p, encoding="utf-8").read()
if "tabel bord" in s:
    print("  was al gepatcht"); raise SystemExit
oud = """            if (now && !was) { until[i] = t + (uint64_t)(pulse_override > 0 ? pulse_override : MAP[i].ms); active[i] = 1; { volatile uint32_t *e0 = entry(MAP[i].board, MAP[i].k); e0[6]++; }"""
nieuw = """            if (now && !was) { until[i] = t + (uint64_t)(pulse_override > 0 ? pulse_override : MAP[i].ms); active[i] = 1; { volatile uint32_t *e0 = entry(MAP[i].board, MAP[i].k); e0[6]++;
                if (log_on) { char b[160]; snprintf(b, sizeof b, "tabel bord %d k %d NA de indruk: [0]=%u [3]=%u [4]=%u [5]=%u [6]=%u", MAP[i].board, MAP[i].k, e0[0], e0[3], e0[4], e0[5], e0[6]); logf_(b); } }"""
assert s.count(oud) == 1, s.count(oud)
s = s.replace(oud, nieuw, 1)
oud2 = """                else { e[3] = 0; e[5] = 0; e[7]++; active[i] = 0; rel_until[i] = t + 25; }"""
nieuw2 = """                else { e[3] = 0; e[5] = 0; e[7]++; active[i] = 0; rel_until[i] = t + 25;
                       if (log_on) { char b[160]; snprintf(b, sizeof b, "tabel bord %d k %d bij LOSLATEN: [0]=%u [3]=%u [4]=%u [5]=%u [7]=%u", MAP[i].board, MAP[i].k, e[0], e[3], e[4], e[5], e[7]); logf_(b); } }"""
assert s.count(oud2) == 1, s.count(oud2)
s = s.replace(oud2, nieuw2, 1)
io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("  hulp uitgebreid met tafellogging")
PY
gcc -shared -fPIC -O2 -o "$SO" "$S" -lpthread 2>&1 | head -5
echo "  gcc exitcode ${PIPESTATUS[0]}"
sha256sum "$SO" | cut -c1-16 | sed 's/^/  hulp sha256 /'

echo
echo "############ 2. de run (240 s, beeld op de tv)"
python3 -c "open('$JVS','wb').write(bytes(64))"
printf '0,0,4' > "$PROBE"
rm -f "$TAPLOG"; : > "$OUT/leven"
cd "$R"
( timeout 420 scripts/play-native.sh --game Halo --gamescope --mute --seconds 240 \
    --bind-ro "$P/probe/libusb-tap.so" /rg/lib/libusb-tap.so \
    --bind-ro "$JVS" /rg/jvs --bind-ro "$PROBE" /rg/tap/probe.txt --bind-rw "$TAPDIR" /rg/tap \
    --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
    --env RG_USB_LOG=/rg/tap/usb-tap.log --env RG_USB_RAW=1 --env RG_USB_MAX=50000 \
    --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1 --env RG_RT_PULSE_MS=2000 \
    --env RG_RT_PROBE=/rg/tap/probe.txt \
    > "$OUT/play.out" 2>&1; echo "$?" > "$OUT/exit" ) &
RUN=$!

sleep 17
PID=$(pgrep -x game 2>/dev/null | head -1)
echo "  game-pid=${PID:-geen}  (t=17s)" | tee -a "$OUT/leven"
# de buitenlezer: dezelfde tabeladressen, van buiten het proces
( [ -n "${PID:-}" ] || exit 0
  exec 3<"/proc/$PID/mem" 2>/dev/null || exit 0
  vorige=""
  for s in $(seq 1 120); do
    regel=""
    for k in 0 3 4 5 6; do
      if dd if=/proc/$PID/mem bs=1 skip=$((TABEL + k*0x50)) count=16 2>/dev/null | xxd -p | head -c 32 > "$OUT/v"; then :; fi
      regel="$regel k$k=$(cat "$OUT/v" 2>/dev/null)"
    done
    if [ "$regel" != "$vorige" ]; then echo "BUITEN t=$((s*2))s $regel" >> "$OUT/tabel.txt"; vorige="$regel"; fi
    sleep 2
  done ) &
LEZER=$!

sleep 3
bit 64
date '+  t=20s START (puls 2000 ms)  %H:%M:%S' | tee -a "$OUT/leven"

# POGING 1: de index
sleep 25      # t=45
for K in 3 4 5; do
  printf '%s' "0,0,$K" > "$PROBE"
  date "+  t=$(( SECONDS ))s POGING 1: TEST op bord 0, k=$K  %H:%M:%S" | tee -a "$OUT/leven"
  puls 1
  sleep 14
  maak_foto "poging1-k$K" | tee -a "$OUT/leven"
done

# POGING 2: het moment -- TEST elke 10 s
printf '0,0,4' > "$PROBE"
date "+  t=$(( SECONDS ))s POGING 2 begint: TEST elke 10 s  %H:%M:%S" | tee -a "$OUT/leven"
for i in $(seq 1 9); do
  puls 1
  sleep 10
done
maak_foto "poging2-eind" | tee -a "$OUT/leven"
date "+  t=$(( SECONDS ))s POGING 2 klaar  %H:%M:%S" | tee -a "$OUT/leven"
echo "  gamelog: $(wc -l < "$G") regels" | tee -a "$OUT/leven"
kill $LEZER 2>/dev/null
wait $RUN 2>/dev/null
echo "  exitcode: $(cat "$OUT/exit" 2>/dev/null)"

echo
echo "############ 3. WAT DE HULP ZAG (tafelwaarden rond elke indruk)"
NAT=$(ls -t ~/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | head -1)
grep -E 'indruk|tabel' "$NAT" 2>/dev/null | tail -40 | sed 's/^/  /'

echo
echo "############ 4. EN VAN BUITEN HET PROCES GELEZEN (dezelfde adressen)"
[ -f "$OUT/tabel.txt" ] && head -20 "$OUT/tabel.txt" | sed 's/^/  /' || echo "  (geen veranderingen gezien van buiten)"

echo
echo "############ 5. WAT DE GAME SCHREEF, en wat het scherm deed"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | tail -12 | sed 's/^/  /'
echo "  --- is er gecrasht?"
tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null | grep -c 'FATAL signal' | sed 's/^/    FATAL-regels: /'

echo
echo "############ 6. opruimen"
pkill -f '/pm/g7/halo/game' 2>/dev/null; pkill -f 'play-native.sh' 2>/dev/null; kill $LEZER 2>/dev/null
sleep 2
pgrep -a -f 'gamescope|wineserver|play-native|/pm/g7/halo/game' 2>/dev/null | grep -v pgrep | sed 's/^/  draait nog: /'
echo "  (leeg = schoon)"
cp -a "$G" "$P/gamelog-run6.txt" 2>/dev/null
[ -f "$TAPLOG" ] && cp -a "$TAPLOG" "$P/usb-tap-run6.log"
for p in "$OUT"/*.png; do [ -f "$p" ] && cp -a "$p" "$P/run6-$(basename "$p")"; done
echo "  bewaard; ruwe rommel: $OUT"
echo "== done"
