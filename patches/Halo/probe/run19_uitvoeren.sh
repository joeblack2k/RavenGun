#!/bin/bash
# RUN 19 = PROEF 2, in de scherpere vorm: spelersaantal 1 EN de aim-kommagetallen DIRECT als bitpatroon.
# Twee runs met een andere waarde (0.5f en 0.25f) zodat "er is een richtpunt" en "het richtpunt beweegt"
# van elkaar te onderscheiden zijn.
# VOORSPELLING VOORAF: met spelersaantal 1 blijft mijn geschreven float staan (of het spel VERBRUIKT hem,
# wat ook bewijs is dat het die adressen leest), en het richtpunt komt van -1,-1 af.
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
D=~/.local/share/ravengun
TAPDIR="$D/tap"
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
JVS="$TAPDIR/start.jvs"
OUT=$(mktemp -d /tmp/run19-XXXX)
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"

maak_foto() { local doel="$OUT/$1.png"
  command -v spectacle >/dev/null && spectacle -b -n -o "$doel" >/dev/null 2>&1 && [ -s "$doel" ]; }
bit() { python3 -c "
import os, struct
fd = os.open('$JVS', os.O_WRONLY); os.pwrite(fd, struct.pack('<I', $1), 8); os.close(fd)"; }
druk() { bit 0; sleep 0.3; bit $1; sleep 0.5; bit 0; }
spel() { python3 - "$1" <<'PY'
import struct, sys
pid=int(sys.argv[1])
with open("/proc/%d/mem"%pid,"rb",0) as f:
    for naam,adr in (("aim x (0x1f392a0)",0x1f392a0),("aim y (0x1f392a4)",0x1f392a4),
                     ("spelersaantal (0x1f392d0)",0x1f392d0),("blok+0 (aanwezig)",0x1a7cb20),
                     ("blok+0x3c (spelernr)",0x1a7cb5c)):
        f.seek(adr); d=f.read(4)
        print("      %-26s u32=%-12d f32=%.6g" % (naam,struct.unpack("<I",d)[0],struct.unpack("<f",d)[0]))
PY
}
# $1 = de float-bits, $2 = het label (bv. 0_5), $3 = de leesbare waarde (bv. 0.5)
een_run() {
  local BITS="$1" LABEL="$2" WAARDE="$3"
  local TABEL="$OUT/rt-invoer-$LABEL.ini"
  sed "s/^as .*0x1f392a0.*/zet     adres=0x1f392a0 breedte=4 waarde=$BITS/; \
       s/^as .*0x1f392a4.*/zet     adres=0x1f392a4 breedte=4 waarde=$BITS/" "$P/rt-invoer.kandidaat.ini" > "$TABEL"
  if ! grep -q "waarde=$BITS" "$TABEL"; then echo "  *** de tabel aanpassen mislukte; ik stop"; return 1; fi
  # het spelersaantal erbij (staat nog niet in de kandidaat-tabel): 1 = het kleinste dat "is nul de poort?" beslist
  grep -q '0x1f392d0' "$TABEL" || \
    echo 'zet     adres=0x1f392d0 breedte=4 waarde=1     # spelersaantal: is NUL de poort? 1 = kleinste > 0' >> "$TABEL"
  if ! grep -q '0x1f392d0' "$TABEL"; then echo "  *** het spelersaantal staat niet in de tabel; ik stop"; return 1; fi
  echo
  echo "############ RUN met aim = $WAARDE ($BITS) -- tabel: $(sha256sum "$TABEL" | cut -c1-16)"
  echo "  --- de regels die de aim zetten:"
  grep -nE '0x1f392a|0x1f392d0' "$TABEL" | sed 's/^/    /'
  local REGELS_VOOR; REGELS_VOOR=$(wc -l < "$G" 2>/dev/null)
  NIEUW() { tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null; }
  python3 -c "open('$JVS','wb').write(bytes(64))"
  cd "$R"
  ( timeout 300 scripts/play-native.sh --game Halo --gamescope --mute --seconds 130 \
      --bind-ro "$P/probe/libusb-tap.so" /rg/lib/libusb-tap.so \
      --bind-ro "$JVS" /rg/jvs --bind-rw "$TAPDIR" /rg/tap \
      --bind-ro "$TABEL" /rg/tabel/rt-invoer.ini \
      --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
      --env RG_USB_LOG=/rg/tap/usb-tap.log --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1 \
      --env RG_RT_PULSE_MS=500 --env RG_RT_TABLE=/rg/tabel/rt-invoer.ini \
      > "$OUT/play-$LABEL.out" 2>&1; echo "$?" > "$OUT/exit-$LABEL" ) &
  local RUN=$!
  sleep 12
  local NAT; NAT=$(ls -t ~/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | head -1)
  grep -iE 'tabel |FOUT' "$NAT" 2>/dev/null | head -2 | sed 's/^/    hulp: /'
  if grep -q 'FOUT: de tabel' "$NAT" 2>/dev/null; then
    echo "  *** DE TABEL IS AFGEKEURD -- er komt geen invoer; ik stop deze run"; return 1; fi
  local T0=$SECONDS A=0
  for i in $(seq 1 30); do
    druk 64
    NIEUW | grep -q 'sound bank: attract' && { A=1; echo "  *** attract na $i pulsen (t=$((SECONDS))s, $(date +%H:%M:%S))" | tee -a "$OUT/leven"; break; }
    [ $((SECONDS-T0)) -gt 60 ] && break
    sleep 2
  done
  if [ "$A" = 0 ]; then echo "  *** geen attract-regel binnen 60 s; deze run is onbeslist"; pkill -f '/pm/g7/halo/game' 2>/dev/null; pkill -f 'play-native.sh' 2>/dev/null; return 1; fi
  sleep 3
  local PID; PID=$(pgrep -x game | head -1)
  maak_foto "19$LABEL-01-bij-attract"
  echo "  --- de waarden MET de tabel actief (is mijn float aangekomen?):"; spel "$PID" | tee -a "$OUT/leven"
  echo "  --- en 15 s later (blijft hij staan, of VERBRUIKT het spel hem?):"
  sleep 15; spel "$PID" | tee -a "$OUT/leven"
  maak_foto "19$LABEL-02-na-15s"
  echo "  --- nieuwe logregels: $(NIEUW | wc -l)"
  NIEUW | tail -4 | sed 's/^/    /'
  wait $RUN 2>/dev/null
  echo "  exitcode: $(cat "$OUT/exit-$LABEL" 2>/dev/null)"
  pkill -f '/pm/g7/halo/game' 2>/dev/null; pkill -f 'play-native.sh' 2>/dev/null; sleep 2
  cp -a "$G" "$P/gamelog-run19$LABEL.txt" 2>/dev/null
  for p in "$OUT"/*"$LABEL"*.png; do [ -f "$p" ] && cp -a "$p" "$P/run19$(basename "$p")"; done
}

echo "############ 0. VOORAF"
date '+  starttijd: %H:%M:%S'
sha256sum "$R/build/librg_rt_input.so" | sed 's/^/  artefact /'
pgrep -a -f 'gamescope|play-native|/pm/g7/halo/game|OpenParrotLoader|amdaemon' | grep -v pgrep | head -3 | sed 's/^/  bezet: /'; echo "  (leeg = vrij)"
echo "  --- de basis-tabel (kandidaat) heeft deze aim-regels:"
grep -nE '^as|^zet.*0x1f392' "$P/rt-invoer.kandidaat.ini" | sed 's/^/    /'
echo "  --- en het spelersaantal dat ik ga zetten: 1 (het kleinste dat de vraag 'is nul de poort?' beslist)"
grep -c '0x1f392d0' "$P/rt-invoer.kandidaat.ini" | sed 's/^/    staat al in de tabel: /'

een_run 1056964608 "0_5"  "0.5f (0x3F000000)"
een_run 1048576000 "0_25" "0.25f (0x3E800000)"

echo
echo "############ SAMENVATTING VAN BEIDE RUNS"
echo "  --- de leesregels uit beide runs staan hierboven; de foto's:"
ls -1 "$P"/run19*.png 2>/dev/null | sed 's/^/    /'
echo "  --- draait er nog iets van mij?"
pgrep -a -f '/pm/g7/halo/game|play-native' | grep -v pgrep | sed 's/^/    /'; echo "    (leeg = schoon)"
echo "== done ($OUT)"
