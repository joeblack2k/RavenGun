#!/bin/bash
# RUN 22 -- DE ANALOOGVAK-PROEF: bereikt een analoog vak van de klasse-0x4-tafel het spel?
#
# WAT IK NU WEET (gemeten in run 21):
#  * Het 0x4axxxx-subsysteem pollt tijdens de attract-lus een LIJST kanalen (teller op 0x1771850, de
#    lijst op 0x1771858). In die lijst staan signaalnummers 596,597,598,599,600,602,607,608,648,652.
#    Met de bewezen formule (bord=(n-0x239)>>5, vak=(n-0x239)&31) zijn dat:
#      596 -> bord 0 vak 27   597 -> bord 0 vak 28   598 -> bord 0 vak 29
#      599 -> bord 0 vak 30   600 -> bord 0 vak 31   602 -> bord 1 vak 1
#      607 -> bord 1 vak 6    608 -> bord 1 vak 7    648 -> bord 2 vak 15   652 -> bord 2 vak 19
#  * De tafel zelf is LEEG op het soortveld na wat wij schrijven. Soort 0 = ANALOOG KANAAL, dus de
#    lus hierboven leest die vakken als ANALOGE kanalen (de schakelaar-getter geeft NULL voor soort 0).
#
# DE PROEF: schrijf in elk van de vijf vakken van bord 0 (27..31) een EIGEN magisch 4-byte getal in alle
# velden NA het soortveld, en zoek dat getal daarna in het HELE beeld van het proces.
#
# VOORSPELLING VOORAF:
#  * SUCCES (het vak bereikt het spel): het magische getal staat niet alleen meer in de tafel, maar ook
#    ERGENS ANDERS -- het spel heeft de waarde overgenomen. Het adres van die tweede plek vertelt waar.
#  * GEEN EFFECT (het vak bereikt het spel niet): het getal staat alleen op de plek waar ik het schreef.
#  * VERKEERD ADRES: dan vindt de zoeker het getal zelfs op de schrijfplek niet -- en dat mag ik niet
#    als "geen effect" tellen. Daarom is de POSITIEVE CONTROLE (vinden op de schrijfplek) verplicht.
#  * Het instrument heeft ook een NEGATIEVE CONTROLE: een magisch getal dat ik NOOIT schrijf mag nergens
#    gevonden worden.
set -u
R=/home/deck/projects/ravengun
P=/home/deck/projects/ravengun-patches/patches/Halo
D=/home/deck/.local/share/ravengun
TAPDIR="$D/tap"; mkdir -p "$TAPDIR"
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
JVS="$TAPDIR/start.jvs"
OUT=$(mktemp -d /tmp/run22-XXXX)
NAS="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam"
START=64
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
RK="python3 $P/probe/rk.py auto"
ZK="python3 $P/probe/zoek.py auto"
bit() { python3 -c "
import os, struct
fd = os.open('$JVS', os.O_WRONLY); os.pwrite(fd, struct.pack('<I', $1), 8); os.close(fd)"; }
druk() { bit 0; sleep 0.25; bit "$1"; sleep 0.45; bit 0; }
nas_afdruk() { local a b
  a=$(timeout 90 find "$NAS/g7" -maxdepth 3 -printf '%p %s %T@\n' 2>/dev/null | sort | md5sum | cut -d' ' -f1)
  b=$(timeout 90 find "$NAS/g7" -maxdepth 3 -printf '%p %s %T@\n' 2>/dev/null | sort | md5sum | cut -d' ' -f1)
  [ "$a" = "$b" ] && echo "    md5 g7-boom: $a (twee keer gelijk)" || echo "    md5: ONBETROUWBAAR ($a tegen $b)"; }

echo "############ RUN 22 -- analoogvak-proef $(date '+%F %H:%M:%S')"
echo "--- IDENTITEIT"
sha256sum "$R/build/librg_rt_input.so" "$R/scripts/play-native.sh" "$P/probe/rk.py" "$P/probe/zoek.py" "$P/probe/run22_uitvoeren.sh" 2>&1 | sed 's/^/    /'
( cd "$R" && git log --oneline -1 ) | cut -c1-60 | sed 's/^/    commit: /'
echo "--- VOORAF: bench vrij?"
NAT=$(pgrep -af 'play-native|/pm/g7/halo/game' || true); WINE=$(pgrep -af 'OpenParrotLoader|Shell.exe|Hodzero|amdaemon' || true)
[ -n "$NAT$WINE" ] && { echo "*** BENCH BEZET"; exit 1; }
echo "    vrij."
echo "--- NAS-vingerafdruk VOOR:"; nas_afdruk

python3 -c "open('$JVS','wb').write(bytes(64))"
cd "$R"
echo "--- START van de game $(date '+%H:%M:%S')"
( timeout 300 scripts/play-native.sh --game Halo --gamescope --mute --seconds 200 \
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
echo "--- START-pulsen tot de attract-lus $(date '+%H:%M:%S')"
REGELS_VOOR=$(wc -l < "$G" 2>/dev/null || echo 0)
NIEUW() { tail -n +$((REGELS_VOOR+1)) "$G" 2>/dev/null; }
A=0; T0=$SECONDS
for i in $(seq 1 30); do
  druk "$START"
  NIEUW | grep -q 'sound bank: attract' && { A=1; echo "    *** attract na $i pulsen (t=$((SECONDS))s)"; break; }
  [ $((SECONDS-T0)) -gt 75 ] && break
  sleep 1.5
done
[ "$A" = 1 ] || echo "*** GEEN attract -- onbeslist"
sleep 2
[ -z "${PID:-}" ] && PID=$(pgrep -x game | head -1)
echo "--- pid=$PID"

# de magische getallen: vak 27..31 van bord 0 -> 0xOOBAD027 .. 0xOOBAD031 ; plus een controlewet die ik NOOIT schrijf
MAGICS="0x0BAD0027 0x0BAD0028 0x0BAD0029 0x0BAD002A 0x0BAD002B"
CONTROLE="0x0BADFFFF"
echo "--- NEGATIEVE CONTROLE VOORAF: komen de magische getallen al van nature voor?"
$ZK $MAGICS $CONTROLE 2>&1 | sed 's/^/    /'

echo
echo "--- DE SCHRIJFACTIE: in elk vak 27..31 de velden NA het soortveld vullen met zijn eigen getal"
for s in 27 28 29 30 31; do
  adres=$((0x1815d20 + s*0x50 + 8))
  waarde=$(printf '0x0BAD%04X' "$s")
  echo "     vak $s -> $adres met $waarde"
  $RK vul "$adres" "$waarde" 6 2>&1 | sed 's/^/       /'
done

echo
echo "--- DE ZOEKTOCHT (positieve controle = vinden op de schrijfplek; negatieve controle = $CONTROLE nergens)"
$ZK $MAGICS $CONTROLE 2>&1 | sed 's/^/    /'

echo
echo "--- en wat staat er nu in de vakken 26..31 van bord 0?"
for s in 26 27 28 29 30 31; do
  echo "     vak $s:"; $RK lees $((0x1815d20 + s*0x50)) 0x50 2>&1 | sed 's/^/       /'
done

echo
echo "--- LEVENSCONTROLE: reageert het spel nog op START? (teller in vak 0)"
for i in 1 2; do druk "$START"; done
sleep 1
$RK lees 0x1815d20 0x50 2>&1 | sed 's/^/    /'
echo "--- nieuwe spellogregels: $(NIEUW | wc -l); laatste 3:"
NIEUW | tail -3 | sed 's/^/    /'

echo; echo "--- wachten op de launcher"
wait "$LAUNCH" 2>/dev/null
echo "    launcher-exitcode: $(cat "$OUT/exit" 2>/dev/null)"
[ -n "${PID:-}" ] && kill "$PID" 2>/dev/null; sleep 2; [ -n "${PID:-}" ] && kill -9 "$PID" 2>/dev/null
echo "--- draait er nog iets van mij?"; pgrep -af 'play-native|/pm/g7/halo/game' | grep -v pgrep | sed 's/^/    /'; echo "    (leeg = schoon)"
echo "--- NAS-vingerafdruk NA:"; nas_afdruk
cp -a "$G" "$P/gamelog-run22.txt" 2>/dev/null
NATLOG=$(ls -t "$D/../../.local/state/ravengun/logs/native-Halo-"*.log 2>/dev/null | head -1)
[ -n "${NATLOG:-}" ] && cp -a "$NATLOG" "$P/run22-launcherlog.txt"
cp -a "$OUT" "$P/run22-uitvoer" 2>/dev/null
echo "############ KLAAR $(date '+%F %H:%M:%S')"
