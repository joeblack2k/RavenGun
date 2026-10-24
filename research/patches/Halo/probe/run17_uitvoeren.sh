#!/bin/bash
# RUN 17: DE GEWEERLAAGPROEF. Volgorde van PROEF-GEWEERLAAG.md:
#   1) START tot de NIEUWE attract-regel (NIEUW())  2) apparaatblok + spelernummer lezen/schrijven
#   3) aim via de muismodus (float-as)              4) de drie trekkerkandidaten een voor een
# Geen herhaal-tot-het-werkt: wat ik lees, staat in het verslag.
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
D=~/.local/share/ravengun
TAPDIR="$D/tap"
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
JVS="$TAPDIR/start.jvs"
TABELK="$P/rt-invoer.kandidaat.ini"
OUT=$(mktemp -d /tmp/run17-XXXX)
REGELS_VOOR=$(wc -l < "$G" 2>/dev/null)
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
NIEUW() { tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null; }
BLOK=$((0x1a7cb20)); SPELNR=$((0x1a7cb5c)); TEL0=$((0x1a7cbe8)); TEL1=$((0x1a7cbec)); TEL3=$((0x1a7cbf4))
AIM=$((0x1f392a0)); SPELERS=$((0x1f392d0))

maak_foto() { local doel="$OUT/$1.png"
  command -v spectacle >/dev/null && spectacle -b -n -o "$doel" >/dev/null 2>&1 && [ -s "$doel" ]; }
bit() { python3 -c "
import os, struct
fd = os.open('$JVS', os.O_WRONLY); os.pwrite(fd, struct.pack('<I', $1), 8); os.close(fd)"; }
druk() { bit 0; sleep 0.3; bit $1; sleep 0.5; bit 0; }
lees() { python3 - "$1" <<'PY'
import struct, sys
pid=int(sys.argv[1])
p=[("blok+0 (aanwezig)",0x1a7cb20),("blok+0x3c (spelernr)",0x1a7cb5c),
   ("teller knop0 (bit7)",0x1a7cbe8),("teller knop1 (bit8)",0x1a7cbec),("teller knop3 (bit9)",0x1a7cbf4),
   ("aim x (f32)",0x1f392a0),("aim y (f32)",0x1f392a4),("spelersaantal [0x1f392d0]",0x1f392d0)]
with open("/proc/%d/mem"%pid,"rb",0) as f:
    for naam,adr in p:
        f.seek(adr); d=f.read(4)
        u=struct.unpack("<I",d)[0]; fl=struct.unpack("<f",d)[0]
        print("    %-26s u32=%-12d f32=%.6g" % (naam,u,fl))
PY
}
stop_en_meld() { echo "  *** $1" | tee -a "$OUT/leven"; maak_foto "stop"
  NIEUW | tail -6 | sed 's/^/    /' | tee -a "$OUT/leven"
  pkill -f '/pm/g7/halo/game' 2>/dev/null; pkill -f 'play-native.sh' 2>/dev/null
  cp -a "$G" "$P/gamelog-run17.txt" 2>/dev/null; cp -a "$OUT/leven" "$P/run17-leven" 2>/dev/null
  for p in "$OUT"/*.png; do [ -f "$p" ] && cp -a "$p" "$P/run17-$(basename "$p")"; done
  echo "== gestopt"; exit 0; }

echo "############ 0. VOORAF: het artefact, de tafel en de bench"
date '+  starttijd: %H:%M:%S'
echo "  --- HET ARTEFACT (moet 8199ef9b zijn):"
sha256sum "$R/build/librg_rt_input.so" 2>/dev/null | sed 's/^/    /'
stat -c '    bron %y' "$R/tools/rt-input-shim/rg_rt_input.c" 2>/dev/null
echo "  --- DE KANDIDAAT-TABEL (sha256 en de regels):"
sha256sum "$TABELK" 2>/dev/null | sed 's/^/    /'
grep -vE '^\s*#|^\s*$' "$TABELK" | sed 's/^/    /'
echo "  --- DE BENCH (native EN wine):"
pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game|OpenParrotLoader|amdaemon|Shell\.exe|Hodzero' | grep -v pgrep | head -4 | sed 's/^/    /'
echo "    (leeg = vrij)"
grep -l '054c' /sys/class/input/event*/id/vendor 2>/dev/null | sed 's/^/    Move: /'; echo "    (leeg = niet aangesloten)"

echo
echo "############ 1. DE RUN (200 s, beeld op de tv, met de kandidaat-tabel)"
python3 -c "open('$JVS','wb').write(bytes(64))"
cd "$R"
( timeout 360 scripts/play-native.sh --game Halo --gamescope --mute --seconds 200 \
    --bind-ro "$P/probe/libusb-tap.so" /rg/lib/libusb-tap.so \
    --bind-ro "$JVS" /rg/jvs --bind-rw "$TAPDIR" /rg/tap \
    --bind-ro "$TABELK" /rg/tabel/rt-invoer.ini \
    --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
    --env RG_USB_LOG=/rg/tap/usb-tap.log --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1 \
    --env RG_RT_PULSE_MS=500 --env RG_RT_TABLE=/rg/tabel/rt-invoer.ini \
    > "$OUT/play.out" 2>&1; echo "$?" > "$OUT/exit" ) &
RUN=$!
sleep 12
NAT=$(ls -t "$D/../state/ravengun/logs"/native-Halo-*.log 2>/dev/null | head -1)
[ -z "${NAT:-}" ] && NAT=$(ls -t ~/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | head -1)
echo "  --- WAT DE HULP VAN DE TABEL ZEGT (dit is de controle op afkeuring):"
grep -iE 'tabel |FOUT|geen tabel' "$NAT" 2>/dev/null | head -4 | sed 's/^/    /'
if grep -q 'FOUT: de tabel' "$NAT" 2>/dev/null; then stop_en_meld "DE TABEL IS AFGEKEURD -- er komt GEEN invoer; ik stop (zie de regel hierboven)"; fi

echo
echo "   --- FASE 1: START tot de NIEUWE attract-regel (max 60 s)"
T0=$SECONDS; ATTRACT=0
for i in $(seq 1 30); do
  druk 64
  if NIEUW | grep -q 'sound bank: attract'; then ATTRACT=1
    echo "  *** NIEUWE attract-regel na $i pulsen (t=$((SECONDS))s, $(date +%H:%M:%S))" | tee -a "$OUT/leven"; break; fi
  [ $((SECONDS-T0)) -gt 60 ] && break
  sleep 2
done
[ "$ATTRACT" = 0 ] && stop_en_meld "DE ATTRACT-LUS IS NIET GEKOMEN binnen 60 s -- dat is de uitkomst"
sleep 3
PID=$(pgrep -x game 2>/dev/null | head -1)
echo "  game-pid=${PID:-geen}; nieuwe regels: $(NIEUW | wc -l)"
maak_foto "01-bij-attract" | tee -a "$OUT/leven"

echo
echo "   --- FASE 2: HET APPARAATBLOK (de tabel zet het vanaf de start; ik LEES of het blijft staan)"
echo "  t=$((SECONDS))s stand 1:"; lees "$PID" | tee -a "$OUT/leven"
sleep 10; echo "  t=$((SECONDS))s stand 2 (10 s later -- is +0 nog 1?):"; lees "$PID" | tee -a "$OUT/leven"
sleep 10; echo "  t=$((SECONDS))s stand 3 (20 s later):"; lees "$PID" | tee -a "$OUT/leven"

echo
echo "   --- FASE 3: DE AIM via de muismodus (bit 7 = de trekkerbron; as = de kommagetallen)"
command -v ravengunctl >/dev/null || RAV=~/projects/ravengun/build/ravengunctl
RAV=${RAV:-ravengunctl}
echo "  --- muizen:"; $RAV mice 2>&1 | head -4 | sed 's/^/    /'
echo "  --- wapenen:"; $RAV arm 2>&1 | head -2 | sed 's/^/    /'
for xy in "0.5 0.5" "0.2 0.2" "0.8 0.8"; do
  echo "  t=$((SECONDS))s test-pattern point $xy" | tee -a "$OUT/leven"
  $RAV test-pattern point $xy 2>&1 | head -2 | sed 's/^/    /'
  sleep 3
  echo "    --- de bron in het gedeelde geheugen (bytes 12/13) en wat er in het spel staat:"
  python3 -c "
d=open('$JVS','rb').read(64); print('      shm[12]=%d shm[13]=%d' % (d[12], d[13]))" 2>/dev/null
  lees "$PID" | tail -3 | tee -a "$OUT/leven"
  maak_foto "aim-$(echo $xy | tr ' .' '__')" | tee -a "$OUT/leven"
done

echo
echo "   --- FASE 4: DE DRIE KANDIDATEN, een voor een (elk voedt een andere teller)"
# trigger -> bit 7 -> teller knop0 ; reload -> bit 8 -> knop1 ; grenade -> bit 9 -> knop3
for koppel in "trigger:7:knop0" "reload:8:knop1" "grenade:9:knop3"; do
  knop=${koppel%%:*}; rest=${koppel#*:}; b=${rest%%:*}; naam=${rest#*:}
  echo "  t=$((SECONDS))s KANDIDAAT $naam: output-button $knop tap (bit $b) x3" | tee -a "$OUT/leven"
  for i in 1 2 3; do $RAV output-button $knop tap 2>&1 | head -1 | sed 's/^/      /'; sleep 2; done
  sleep 2
  lees "$PID" | sed -n '3,5p' | tee -a "$OUT/leven"
  maak_foto "kandidaat-$naam" | tee -a "$OUT/leven"
done

echo
echo "   --- FASE 5: de eindstand"
lees "$PID" | tee -a "$OUT/leven"
maak_foto "99-eind" | tee -a "$OUT/leven"
echo "  gamelog aan het eind: $(NIEUW | wc -l) nieuwe regels"
wait $RUN 2>/dev/null
echo "  exitcode: $(cat "$OUT/exit" 2>/dev/null)"

echo
echo "############ 2. DE NIEUWE LOGBOEKREGELS VAN DEZE RUN"
NIEUW | sed 's/^/  /'

echo
echo "############ 3. WAT DE HULP MELDDE (tabel, indrukken, assen)"
grep -iE 'tabel |indruk bit|as |teller|zet ' "$NAT" 2>/dev/null | tail -20 | sed 's/^/  /'

echo
echo "############ 4. DE IDENTITEITSREGEL"
head -7 "$NAT" 2>/dev/null | sed 's/^/  /'

echo
echo "############ 5. opruimen (alleen mijn eigen pids) + de kern"
$RAV disarm 2>&1 | head -1 | sed 's/^/  /'
pkill -f '/pm/g7/halo/game' 2>/dev/null; pkill -f 'play-native.sh' 2>/dev/null; sleep 2
pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game' 2>/dev/null | grep -v pgrep | sed 's/^/  draait nog: /'; echo "  (leeg = schoon)"
sha256sum ~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/TeknoParrot/TeknoParrot64.dll 2>/dev/null | cut -c1-16 | sed 's/^/  kern: /'
cp -a "$G" "$P/gamelog-run17.txt" 2>/dev/null; cp -a "$OUT/leven" "$P/run17-leven" 2>/dev/null
[ -n "${NAT:-}" ] && cp -a "$NAT" "$P/run17-launcherlog.txt" 2>/dev/null
for p in "$OUT"/*.png; do [ -f "$p" ] && cp -a "$p" "$P/run17-$(basename "$p")"; done
ls -1 "$P"/run17-*.png 2>/dev/null | sed 's/^/  /'
echo "  --- ruwe rommel: $OUT"
echo "== done"
