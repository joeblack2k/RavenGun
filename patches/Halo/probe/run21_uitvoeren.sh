#!/bin/bash
# RUN 21 -- DE AIM-BRUG PROBEREN: is de weg die het spel per beeld voor de AIM gebruikt levend?
#
# WAT IK NU WEET (statisch gelezen, zie IO-BRUG-ROUTE.md):
#  * De per-beeld-invoervuller 0x624120 haalt per speler TWEE ANALOGE kanalen op, via de analoog-getter
#    0x61ee70, met kanaalnummers uit rodata: speler 1 = 793 (0x319) en 794 (0x31a). Die twee nummers
#    komen NIET in de 32-vaks-tafel uit; de getter geeft vaste adressen terug: 0x181d418 en 0x181d538.
#  * Die hele lus wordt overgeslagen als [0x1f392d0] (spelersaantal) <= 0 -- en dat is in onze runs 0.
#  * Verder in de lus: de aim (uit 0xdd6c60, dus het paar op 0x1f392a0) wordt vergeleken met de floats
#    op kanaal+0x14 en kanaal+0x18; slaagt die vergelijking, dan zet het spel bit 0x10000 in de
#    per-spelerstructuur [0x181f7e0] + speler*0x48 (velden +0 en +0x120)  (0x62424b en 0x624251).
#
# VOORSPELLING VOORAF (per fase, met wat succes / geen effect / verkeerd adres zou zijn):
#  FASE 0 (attract, niets geschreven): de vlaggen moeten 0x10000 NIET hebben, het spelersaantal 0 zijn.
#     Een niet-nul [0x181f7e0] is nodig, anders kan de aim-logica niet eens schrijven -> dan stop ik.
#  FASE 1 (spelersaantal 0x1f392d0 = 4): verwachting GEEN EFFECT op de vlaggen, want de lus zoekt eerst
#     per speler een apparaatblok (0xdd4b10 op 0x1a7cb20) en dat is er niet -> 0x624360.
#  FASE 2 (+ apparaatblok 0 = aanwezig, spelernummer 0, poortbyte 0): nu loopt het lijf van de lus wel.
#     Verwachting: de vlaggen kunnen veranderen. SUCCES = bit 0x10000 verschijnt; dan is bewezen dat de
#     aim-logica onze ingangen heeft verbruikt. GEEN EFFECT = nog niet genoeg (de vergelijking faalt).
#  FASE 3 (+ de grenzen van kanaal 793/794 en het aim-paar): SUCCES = bit 0x10000 in de vlaggen van
#     speler 0. GEEN EFFECT = de weg is niet levend of mijn model van de poort is fout -- dan meld ik
#     de geschreven waarden en de gelezen waarden, en noem ik het een ondersteund negatief.
#  Het instrument zelf moet zich eerst bewijzen: rk.py leest de ELF-kop, laat een ongemapt adres FALEN,
#  en leest na ELKE schrijfactie hetzelfde adres TERUG (schrijf landt / landt niet).
set -u
R=/home/deck/projects/ravengun
P=/home/deck/projects/ravengun-patches/patches/Halo
D=/home/deck/.local/share/ravengun
TAPDIR="$D/tap"; mkdir -p "$TAPDIR"
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
JVS="$TAPDIR/start.jvs"
OUT=$(mktemp -d /tmp/run21-XXXX)
NAS="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam"
START=64
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
RK="python3 $P/probe/rk.py auto"

bit() { python3 -c "
import os, struct
fd = os.open('$JVS', os.O_WRONLY); os.pwrite(fd, struct.pack('<I', $1), 8); os.close(fd)"; }
druk() { bit 0; sleep 0.25; bit "$1"; sleep 0.45; bit 0; }
foto() { local doel="$OUT/$1.png"
  command -v spectacle >/dev/null && spectacle -b -n -o "$doel" >/dev/null 2>&1 && [ -s "$doel" ]; }
nas_afdruk() {  # twee keer; alleen gelijk = betrouwbaar (CIFS geeft soms een korte lijst)
  local a b
  a=$(timeout 90 find "$NAS/g7" -maxdepth 3 -printf '%p %s %T@\n' 2>/dev/null | sort | md5sum | cut -d' ' -f1)
  b=$(timeout 90 find "$NAS/g7" -maxdepth 3 -printf '%p %s %T@\n' 2>/dev/null | sort | md5sum | cut -d' ' -f1)
  if [ "$a" = "$b" ]; then echo "    md5 g7-boom: $a  (twee keer gelijk = betrouwbaar)"
  else echo "    md5 g7-boom: ONBETROUWBAAR ($a tegen $b; CIFS gaf een korte lijst)"; fi; }
rk() { $RK "$@" 2>&1 | sed 's/^/    /'; }

echo "############ RUN 21 -- de aim-brug (per-beeld-invoervuller) $(date '+%F %H:%M:%S')"
echo "--- IDENTITEIT"
sha256sum "$R/build/librg_rt_input.so" "$R/scripts/play-native.sh" "$P/probe/rk.py" "$P/probe/run21_uitvoeren.sh" 2>&1 | sed 's/^/    /'
( cd "$R" && git log --oneline -1 ) | cut -c1-60 | sed 's/^/    commit: /'
ls -la /tmp/io-brug-game.bin | sed 's/^/    bronbestand: /'
echo "    tabel: GEEN (de ingebouwde standaard; alleen kastschakelaars). De proef schrijft met rk.py."
echo "--- VOORAF: bench vrij?"
NAT=$(pgrep -af 'play-native|/pm/g7/halo/game' || true); WINE=$(pgrep -af 'OpenParrotLoader|Shell.exe|Hodzero|amdaemon' || true)
echo "    native: ${NAT:-  (leeg)}"; echo "    wine:   ${WINE:-  (leeg)}"
[ -n "$NAT$WINE" ] && { echo "*** BENCH BEZET -- ik start niet"; exit 1; }
echo "--- NAS-vingerafdruk VOOR:"; nas_afdruk

python3 -c "open('$JVS','wb').write(bytes(64))"
REGELS_VOOR=$(wc -l < "$G" 2>/dev/null || echo 0)
NIEUW() { tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null; }

echo; echo "--- START van de game $(date '+%H:%M:%S')"
cd "$R"
( timeout 300 scripts/play-native.sh --game Halo --gamescope --mute --seconds 230 \
    --bind-ro "$JVS" /rg/jvs \
    --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so \
    --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1 \
    > "$OUT/play.out" 2>&1; echo "$?" > "$OUT/exit" ) &
LAUNCH=$!
sleep 11
PID=$(python3 -c "
import os
for p in os.listdir('/proc'):
    if p.isdigit():
        try: e=os.readlink('/proc/%s/exe'%p)
        except OSError: continue
        if e.endswith('halo/game'): print(p); break
")
echo "--- FASE 0a: de lezer op een LEVEND proces (vroeg, t=11 s) pid=${PID:-?}"
[ -n "${PID:-}" ] && rk rapport

echo "--- START-pulsen tot de attract-lus $(date '+%H:%M:%S')"
A=0; T0=$SECONDS
for i in $(seq 1 30); do
  druk "$START"
  NIEUW | grep -q 'sound bank: attract' && { A=1; echo "    *** attract na $i pulsen (t=$((SECONDS))s)"; break; }
  [ $((SECONDS-T0)) -gt 75 ] && break
  sleep 1.5
done
[ "$A" = 1 ] || { echo "*** GEEN attract -- onbeslist"; }
sleep 2
[ -z "${PID:-}" ] && PID=$(pgrep -x game | head -1)
echo "--- FASE 0b: BIJ DE ATTRACT-LUS (basislijn, niets geschreven) $(date '+%H:%M:%S')"
rk rapport
echo "--- en de runtime-lijst van het 0x4axxxx-subsysteem (0x1771850):"
rk lijst 0x1771850
foto "21-00-basislijn"

echo
echo "--- FASE 1: ALLEEN het spelersaantal op 4"
rk schrijf 0x1f392d0 u32 4
sleep 4
rk rapport

echo
echo "--- FASE 2: + apparaatblok 0 aanwezig (0x1a7cb20=1, 0x1a7cb5c=0) en de poort open (0x1aa37c0=0, 0x1aa37a0=0)"
rk schrijf 0x1a7cb20 u32 1
rk schrijf 0x1a7cb5c u32 0
rk schrijf 0x1aa37c0 u32 0
rk schrijf 0x1aa37a0 u32 0
sleep 4
rk rapport

echo
echo "--- FASE 3a: de grenzen van de twee analoogkanalen ZO zetten dat de aim BUITEN het bereik valt"
echo "     (0x62420c: als A[+0x14] > x, dan SPRINGT het spel naar 0x624238 en ZET het bit 0x10000)"
echo "     VOORSPELLING: bit 0x10000 wordt GEZET in [0x181f7e0]+0 en +0x120 van speler 0."
rk schrijf 0x181d42c f32 1000000000
rk schrijf 0x181d430 f32 1000000000
rk schrijf 0x181d54c f32 -1000000000
rk schrijf 0x1f392a0 f32 0.5
rk schrijf 0x1f392a4 f32 0.5
sleep 4
rk rapport
echo "--- FASE 3b: dezelfde aim, maar de grens ZO dat de aim BINNEN het bereik valt"
echo "     (0x6243f0: als B[+0x14] <= y, dan WIST het spel bit 0x10000)"
echo "     VOORSPELLING: bit 0x10000 wordt GEWIST. Zet-en-wis op onze schrijfacties = bewijs dat de"
echo "     aim-logica onze ingangen verbruikt."
rk schrijf 0x181d42c f32 -1000000000
rk schrijf 0x181d430 f32 1000000000
rk schrijf 0x181d54c f32 -1000000000
sleep 4
rk rapport
echo "--- FASE 3c: nog een keer ZETTEN (de omgekeerde richting, tegen een toevalstreffer)"
rk schrijf 0x181d42c f32 1000000000
sleep 4
rk rapport
foto "21-03-na-fase3"
echo "--- en de tafel van bord 0 (heeft het spel zelf iets aan de vakken veranderd?)"
rk tafel

echo
echo "--- LEVENSCONTROLE: het spel moet nog op START reageren (teller in vak 0 van bord 0)"
for i in 1 2; do druk "$START"; done
sleep 1
rk lees 0x1815d20 0x50
echo "--- en het spellogboek: nieuwe regels $(NIEUW | wc -l), laatste 4:"
NIEUW | tail -4 | sed 's/^/    /'
echo "--- wat de hulp zelf meldde:"
grep -a 'rg_rt_input: ' "$G" 2>/dev/null | tail -6 | sed 's/^/    /'

echo; echo "--- wachten op de launcher"
wait "$LAUNCH" 2>/dev/null
echo "    launcher-exitcode: $(cat "$OUT/exit" 2>/dev/null)"
[ -n "${PID:-}" ] && kill "$PID" 2>/dev/null; sleep 2; [ -n "${PID:-}" ] && kill -9 "$PID" 2>/dev/null
echo "--- draait er nog iets van mij?"; pgrep -af 'play-native|/pm/g7/halo/game' | grep -v pgrep | sed 's/^/    /'; echo "    (leeg = schoon)"
echo "--- NAS-vingerafdruk NA:"; nas_afdruk

for f in "$OUT"/*.png; do [ -f "$f" ] && cp -a "$f" "$P/run21-$(basename "$f")"; done
cp -a "$G" "$P/gamelog-run21.txt" 2>/dev/null
NATLOG=$(ls -t "$D/../../.local/state/ravengun/logs/native-Halo-"*.log 2>/dev/null | head -1)
[ -n "${NATLOG:-}" ] && cp -a "$NATLOG" "$P/run21-launcherlog.txt"
cp -a "$OUT" "$P/run21-uitvoer" 2>/dev/null
echo "############ KLAAR $(date '+%F %H:%M:%S')"
