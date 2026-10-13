#!/bin/bash
# RUN 20 -- DE MASKERLEZING (taak 1) plus de eerste tafelkaart, PLUS de kalibratie van mijn lezer.
#
# VOORSPELLING VOORAF (opgeschreven voordat er iets draait):
#  * De lezer memlees.py moet K1..K4 halen (ELF-kop op 0x400000, bestand==proces, ongemapt adres
#    moet FALEN). Zakt een kalibratie, dan is ELKE waarde hieronder onbruikbaar en meld ik dat.
#  * De hulp schrijft op 0x1815d20 + k*0x50 (bord 0; bewezen adresmodel: 0x180ae80+0xaea0). De
#    START-indruk van de hulp (bit 6 -> bord 0 k 0) moet daar [6] (de indruk-teller) met 1 verhogen.
#    Zegt mijn externe lezer hetzelfde als het eigen logboek van de hulp, dan is de lezer op een LEVEND
#    adres bewezen, niet alleen op de ELF-kop.
#  * De twee maskers 0x1766f40 en 0x181d8a0 worden GELEZEN, niet geschreven; er is geen "effect" om te
#    zien. Succes = een gekalibreerde waarde plus de uitslag van bit 0x8. Wat de waarden zullen zijn,
#    voorspel ik NIET (daar gaat het juist om).
#  * De tafelkaart: ik verwacht dat de getter-tafel (0x180ae80) vakken met [0] != 0 laat zien (de
#    soortvelden van de kastschakelaars) en dat de hulp-tafel (0x1812d20) precies de vakken laat zien
#    die de hulp heeft aangeraakt. BLIJKT 0x1812d20 == de tafel van de getter, dan is het adresmodel
#    opgelost en is 0xaea0 de bord-0-offset in een groter blok.
#  * Wat een NO-EFFECT zou zijn: als de lezer op 0x1812d20 niets ziet bewegen terwijl de hulp zijn
#    eigen logboek wel "indruk ... [6]=n" laat zien, dan leest mijn lezer het verkeerde adres.
set -u
R=/home/deck/projects/ravengun
P=/home/deck/projects/ravengun-patches/patches/Halo
D=/home/deck/.local/share/ravengun
TAPDIR="$D/tap"; mkdir -p "$TAPDIR"
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
JVS="$TAPDIR/start.jvs"
OUT=$(mktemp -d /tmp/run20-XXXX)
NAS="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam"
START=64                      # bit 6 = p1.start
VROEG=12                      # seconde waarop de eerste (vroege) lezing valt
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"

bit() { python3 -c "
import os, struct
fd = os.open('$JVS', os.O_WRONLY); os.pwrite(fd, struct.pack('<I', $1), 8); os.close(fd)"; }
druk() { bit 0; sleep 0.25; bit "$1"; sleep 0.45; bit 0; }
foto() { local doel="$OUT/$1.png"
  command -v spectacle >/dev/null && spectacle -b -n -o "$doel" >/dev/null 2>&1 && [ -s "$doel" ]; }

nas_afdruk() { timeout 60 find "$NAS/g7" -maxdepth 3 -printf '%p %s %T@\n' 2>/dev/null | sort | md5sum | sed 's/^/    md5 g7-boom: /'; }

echo "############ RUN 20 -- maskerlezing + tafelkaart + lezerkalibratie"
echo "  starttijd: $(date '+%F %H:%M:%S')"
echo "--- IDENTITEIT (wie doet deze run)"
sha256sum "$R/build/librg_rt_input.so" "$R/scripts/play-native.sh" "$P/probe/memlees.py" "$P/probe/pols.py" "$P/probe/run20_uitvoeren.sh" 2>&1 | sed 's/^/    /'
( cd "$R" && git log --oneline -1 ) | sed 's/^/    commit: /'
ls -la /tmp/io-brug-game.bin 2>&1 | sed 's/^/    bronbestand lezer: /'
echo "    tabel: GEEN (de ingebouwde standaard van de hulp; alleen kastschakelaars)"

echo "--- VOORAF: beide kanten van de bench vrij?"
NAT=$(pgrep -af 'play-native|/pm/g7/halo/game' || true)
WINE=$(pgrep -af 'OpenParrotLoader|Shell.exe|Hodzero|amdaemon' || true)
echo "    native: ${NAT:-  (leeg = vrij)}"
echo "    wine:   ${WINE:-  (leeg = vrij)}"
if [ -n "$NAT$WINE" ]; then echo "*** BENCH NIET VRIJ -- ik start niet"; exit 1; fi
echo "--- NAS-vingerafdruk VOOR:"; nas_afdruk

python3 -c "open('$JVS','wb').write(bytes(64))"
REGELS_VOOR=$(wc -l < "$G" 2>/dev/null || echo 0)
NIEUW() { tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null; }

echo
echo "--- START van de game $(date '+%H:%M:%S')"
cd "$R"
( timeout 300 scripts/play-native.sh --game Halo --gamescope --mute --seconds 200 \
    --bind-ro "$JVS" /rg/jvs \
    --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so \
    --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1 \
    > "$OUT/play.out" 2>&1; echo "$?" > "$OUT/exit" ) &
LAUNCH=$!
MY_PIDS=""

sleep "$VROEG"
PID=$(python3 -c "
import os
for p in os.listdir('/proc'):
    if p.isdigit():
        try: e=os.readlink('/proc/%s/exe'%p)
        except OSError: continue
        if e.endswith('halo/game'): print(p); break
")
if [ -n "$PID" ]; then
  MY_PIDS="$PID"
  echo "--- LEZING 1 (vroeg, t=$VROEG s, voor de attract-lus) pid=$PID"
  python3 "$P/probe/memlees.py" "$PID" "$OUT" "vroeg" 2>&1 | sed 's/^/    /'
else
  echo "--- LEZING 1 overgeslagen: nog geen game-proces na ${VROEG}s"
fi

# De START-lus, met de pols op het vak van de hulp OP DE ACHTERGROND: dat is de levende kalibratie.
if [ -n "${PID:-}" ]; then
  nohup python3 "$P/probe/pols.py" "$PID" 0x1815d20 80 25 > "$OUT/pols.txt" 2>&1 &
  POLS=$!
else
  POLS=""
fi

echo "--- START-pulsen tot de attract-lus $(date '+%H:%M:%S')"
A=0; T0=$SECONDS
for i in $(seq 1 30); do
  druk "$START"
  if NIEUW | grep -q 'sound bank: attract'; then
    A=1; echo "    *** attract na $i pulsen (t=$((SECONDS))s, $(date '+%H:%M:%S'))"; break
  fi
  [ $((SECONDS-T0)) -gt 70 ] && break
  sleep 1.5
done
if [ "$A" = 1 ]; then
  sleep 2
  if [ -z "${PID:-}" ]; then
    PID=$(python3 -c "
import os
for p in os.listdir('/proc'):
    if p.isdigit():
        try: e=os.readlink('/proc/%s/exe'%p)
        except OSError: continue
        if e.endswith('halo/game'): print(p); break
")
  fi
  MY_PIDS="$PID"
  echo "--- LEZING 2 (BIJ DE ATTRACT-LUS) pid=$PID $(date '+%H:%M:%S')"
  python3 "$P/probe/memlees.py" "$PID" "$OUT" "attract1" 2>&1 | sed 's/^/    /'
  foto "20-01-bij-attract"
  echo "--- wacht 20 s en lees NOG EENS (wat beweegt er vanzelf?)"
  sleep 20
  echo "--- LEZING 3 (attract + 20 s) $(date '+%H:%M:%S')"
  python3 "$P/probe/memlees.py" "$PID" "$OUT" "attract2" 2>&1 | sed 's/^/    /'
  foto "20-02-na-20s"
else
  echo "    *** GEEN attract-regel binnen de tijd -- deze run is onbeslist over de attract-lus"
fi

# de pols netjes afronden
if [ -n "${POLS:-}" ]; then wait "$POLS" 2>/dev/null || true; fi
echo
echo "--- DE LEVENDE KALIBRATIE: wat zag mijn externe lezer op 0x1812d20 terwijl de hulp START indrukte?"
sed 's/^/    /' "$OUT/pols.txt" 2>/dev/null | head -40
echo "--- en wat zegt de HULP zelf in het spellogboek?"
grep -a 'rg_rt_input: indruk' "$G" 2>/dev/null | tail -12 | sed 's/^/    /'
echo "--- en de tabelmelding van de hulp:"
grep -a 'rg_rt_input: tabel ' "$G" 2>/dev/null | sed 's/^/    /'
grep -a 'rg_rt_input: FOUT' "$G" 2>/dev/null | sed 's/^/    /'
echo "--- nieuwe spellogregels: $(NIEUW | wc -l); de laatste 5:"
NIEUW | tail -5 | sed 's/^/    /'

echo
echo "--- wachten op het einde van de launcher"
wait "$LAUNCH" 2>/dev/null
echo "    launcher-exitcode: $(cat "$OUT/exit" 2>/dev/null)"
for p in $MY_PIDS; do kill "$p" 2>/dev/null; done
sleep 2
for p in $MY_PIDS; do kill -9 "$p" 2>/dev/null; done

echo
echo "--- opruimen: draait er nog iets van mij?"
pgrep -af 'play-native|/pm/g7/halo/game' | grep -v pgrep | sed 's/^/    /'; echo "    (leeg = schoon)"
echo "--- NAS-vingerafdruk NA:"; nas_afdruk

for f in "$OUT"/*.png; do [ -f "$f" ] && cp -a "$f" "$P/run20-$(basename "$f")"; done
cp -a "$G" "$P/gamelog-run20.txt" 2>/dev/null
NATLOG=$(ls -t "$D/../../.local/state/ravengun/logs/native-Halo-"*.log 2>/dev/null | head -1)
[ -n "${NATLOG:-}" ] && cp -a "$NATLOG" "$P/run20-launcherlog.txt"
cp -a "$OUT" "$P/run20-uitvoer" 2>/dev/null
echo "############ KLAAR $(date '+%F %H:%M:%S')  (uitvoer in $OUT, gekopieerd naar $P/run20-uitvoer)"
