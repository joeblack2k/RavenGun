#!/bin/bash
# koppel3-meting.sh - METING 3: dezelfde A/B als meting 2, maar met /dev/bus/usb TERUG erin.
#
#   bash koppel3-meting.sh            # voorbereiding + proef (bench-vrij). START NIETS.
#   bash koppel3-meting.sh --start    # de run (max 90 s). START DE GAME.
#
# WAAROM DEZE RUN. In meting 2 verbouwde ik /dev (om een knoop te kunnen toevoegen) en vergat
# /dev/bus/usb. Daardoor brak de USB-opsomming van het spel, verschenen er drie NIEUWE fouten
# (opendir /dev/bus/usb, /proc/bus/usb, "") en viel de daling van 89 naar 51 regels en van 409 naar 385
# ATTACH's niet toe te schrijven aan de twee toevoegingen. Deze run zet dat recht met EEN wijziging:
#   --dev-bind-try /dev/bus/usb /dev/bus/usb
# Verder is alles identiek aan meting 2 (koppeling /g3 + /T4User-overlay, de kopie van syslog_bb3.txt in
# de werkmap, en de synthetische /dev/sierra2 als PTY).
#
# VOORAF VASTGELEGD WAT IK VERWACHT (zodat ik het kan weerleggen in plaats van het te redden):
#   * de drie nieuwe fouten van meting 2 verdwijnen -> FOUT van 11 naar ongeveer 8
#     (resterend: PhysX.conf 2x, nethasp 3x, /dev/js0, /dev/ttyS0 EPERM, /dev/dsp)
#   * het aantal regels komt terug in de buurt van de 89 van meting 1
#   * de ATTACH's komen terug in de buurt van de 409 van meting 1
#   * de lus blijft een metronoom van 0,011 s met 0,4 ms mediane levensduur en gelijktijdigheid 2
# Als de regels NIET terugkomen in de buurt van 89, dan was mijn verklaring van meting 2 fout en is er
# nog iets anders veranderd -- dat meld ik dan zo.
set -u

R="$HOME/projects/ravengun"
LOADER="$R/tools/rg-elfloader/rg-elfloader"
NASDIR="/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation "
GAME="$NASDIR/game"
EL="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2"
LB32="$HOME/.local/share/ravengun/runtime/lib32"
D="$HOME/projects/ravengun-patches/patches/Terminator"
INC="$R/tools/rg-elfloader/inc"
W=/tmp/ts-koppel3
DO_START=0
[ "${1:-}" = "--start" ] && DO_START=1
VERW_SHA_GAME=798dec07a71c5c37703242edab6dce6d5a7e018b8720a85986842e3ef99b206a
DOEL_KERN=f5c7532d6e63848564a4b9233d30706793066d94d3bbafafe45bf72c2b1d6e28
KERN="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/TeknoParrot/TeknoParrot64.dll"
PLAFOND=90
M1="$D/logs-koppel-20261013-1201/B.bestand.log"
M2="$D/logs-koppel2-20261013-1211/B.bestand.log"
mkdir -p "$W/log" "$W/upper" "$W/work" "$W/view"
ok(){ printf '  OK    %s\n' "$*"; }
warn(){ printf '  LET OP %s\n' "$*"; }
kop(){ printf '\n=== %s ===\n' "$*"; }
SOCAT=""

# De benchcontrole, nu met de namen die er ECHT staan. gamescope-wl en een los `game`-proces zijn de
# twee die mijn oude controle miste -- daarmee had ik de bench kunnen delen.
bench_bezet(){
  local p
  for p in gamescope gamescope-wl rg-elfloader wine wine64 BudgieLoader VACUUM.exe game; do
    pgrep -x "$p" >/dev/null 2>&1 && { echo "$p"; return 0; }
  done
  return 1
}

kop "VOORBEREIDING"
if b=$(bench_bezet); then
  warn "BENCH BEZET door: $b  -- ik start NIET en wacht liever dan te delen"
  [ "$DO_START" = 1 ] && exit 1
else
  ok "bench vrij"
fi
sha=$(sha256sum "$GAME" | cut -d' ' -f1)
[ "$sha" = "$VERW_SHA_GAME" ] && ok "sha256 gast klopt" || warn "sha256 wijkt af: $sha"
printf '  lader: %s\n' "$(sha256sum "$LOADER" | cut -d' ' -f1)"
printf '  kern : %s\n' "$(sha256sum "$KERN" | cut -d' ' -f1)"
for f in bestand-log.c ptracer-permit.c fork-log.c ptrace-log.c; do
  out="libravengun-${f%.c}.so"
  gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/$out" "$D/$f" -ldl 2>&1 | head -3
  [ -s "$W/$out" ] && ok "$out" || warn "$out NIET gebouwd"
done
cp "$GAME" "$W/view/game"
for d in T4Data ServiceDongleApp hasp env irgun GeneralPurposeStateMachine.txt; do
  [ -e "$NASDIR/$d" ] && ln -sfn "$NASDIR/$d" "$W/view/$d"
done
[ -e "$NASDIR/T4User" ] && cp -r "$NASDIR/T4User" "$W/view/T4User"
cp "$NASDIR/T4User/log/syslog_bb3.txt" "$W/view/syslog_bb3.txt" 2>/dev/null && \
  ok "syslog_bb3.txt in de werkmap (kopie uit de dump, $(stat -c%s "$W/view/syslog_bb3.txt") B)" || \
  warn "geen syslog_bb3.txt in de dump"
if command -v socat >/dev/null 2>&1; then
  socat -d -d pty,raw,echo=0,link="$W/sierra2" pty,raw,echo=0,link="$W/sierra2-peer" >"$W/socat.log" 2>&1 &
  SOCAT=$!
  for i in $(seq 1 20); do [ -L "$W/sierra2" ] && break; sleep 0.2; done
  [ -L "$W/sierra2" ] && ok "PTY: $W/sierra2 -> $(readlink -f "$W/sierra2") (eigen socat-pid $SOCAT)" || warn "geen PTY"
fi

BW=(bwrap --die-with-parent
    --ro-bind /usr /usr
    --symlink usr/lib /lib --symlink usr/lib64 /lib64 --symlink usr/bin /bin --symlink usr/bin /sbin
    --ro-bind /etc /etc --proc /proc --ro-bind /sys /sys
    --ro-bind /home /home --ro-bind /var /var --ro-bind /opt /opt --ro-bind /boot /boot --ro-bind /srv /srv)
[ -e /root ] && BW+=(--ro-bind /root /root)
BW+=(--dev /dev)
BW+=(--dev-bind-try /dev/pts /dev/pts)
# DE FIX VAN DEZE METING: /dev/bus/usb terug, zodat de apparaatweergave een superset is van meting 1.
BW+=(--dev-bind-try /dev/bus/usb /dev/bus/usb)
for dn in null zero full random urandom tty shm dri snd ttyS0 input; do
  [ -e "/dev/$dn" ] && BW+=(--dev-bind-try "/dev/$dn" "/dev/$dn")
done
[ -L "$W/sierra2" ] && [ -n "$SOCAT" ] && BW+=(--dev-bind "$(readlink -f "$W/sierra2")" /dev/sierra2)
BW+=(--ro-bind /run /run --bind-try /run/user/1000 /run/user/1000 --bind /tmp /tmp)
BW+=(--dir /g3 --ro-bind "$NASDIR" /g3)
BW+=(--overlay-src "$NASDIR/T4User" --overlay "$W/upper" "$W/work" /T4User)

kop "DE PROEF (bench-vrij)"
uit=$("${BW[@]}" /bin/sh -c '
  ls /g3/T4Data/etranslation/english.csv >/dev/null 2>&1 && echo "G3:leesbaar" || echo "G3:NIET"
  head -c 20 /T4User/log/syslog_bb3.txt >/dev/null 2>&1 && echo "T4:leest-bron" || echo "T4:leest-NIET"
  echo proef > /T4User/log/t4proef.txt 2>/dev/null && echo "T4:schrijft" || echo "T4:schrijft-NIET"
  touch "/home/deck/mnt/spidernas_emulation/ts-proef-niet-mogen.txt" 2>/dev/null && echo "NAS:SCHRIJFBAAR" || echo "NAS:alleen-lezen"
  ls -l /dev/sierra2 >/dev/null 2>&1 && echo "SIERRA:aanwezig" || echo "SIERRA:afwezig"
  opendir() { ls -d "$1" >/dev/null 2>&1; }
  opendir /dev/bus/usb && echo "USB:aanwezig" || echo "USB:AFWEZIG"
  cd /tmp/ts-koppel3/view && (head -c 20 syslog_bb3.txt >/dev/null 2>&1 && echo "RELSYS:leesbaar" || echo "RELSYS:NIET")
  cd /tmp/ts-koppel3/view && (echo x >> syslog_bb3.txt 2>/dev/null && echo "RELSYS:schrijfbaar" || echo "RELSYS:niet-schrijfbaar")
' 2>&1)
printf '%s\n' "$uit" | sed 's/^/  /'
for p in "G3:leesbaar" "T4:leest-bron" "T4:schrijft" "NAS:alleen-lezen" "SIERRA:aanwezig" "USB:aanwezig" "RELSYS:leesbaar" "RELSYS:schrijfbaar"; do
  printf '%s' "$uit" | grep -q "$p" && ok "$p" || warn "$p ONTBREEKT"
done
[ -e "$W/upper/log/t4proef.txt" ] && ok "T4User-schrijfactie landt LOKAAL in de overlay" || warn "geen upper-schrijfactie"
[ -e "$NASDIR/T4User/log/t4proef.txt" ] && { warn "OP DE NAS!"; rm -f "$NASDIR/T4User/log/t4proef.txt"; } || ok "NIET op de NAS"

if [ "$DO_START" != 1 ]; then
  kop "KLAAR"; echo "  Draai met: bash $0 --start"
  [ -n "$SOCAT" ] && { kill "$SOCAT" 2>/dev/null; echo "  (eigen socat-pid $SOCAT gestopt)"; }
  exit 0
fi

kop "DE RUN (max ${PLAFOND}s)"
cat >"$W/binnen.sh" <<EOF
cd "$W/view" || exit 1
export LD_LIBRARY_PATH="$EL/libs:$LB32"
export RG_ELFLOADER_VERBOSE=1
export RAVENGUN_BESTAND_LOG="$W/log/B.bestand.log"
export RAVENGUN_FORK_LOG="$W/log/B.fork.log"
export RAVENGUN_PTRACE_LOG="$W/log/B.ptrace.log"
export RAVENGUN_PTRACER_LOG="$W/log/B.permit.log"
exec timeout $PLAFOND env LD_PRELOAD="$W/libravengun-ptracer-permit.so:$W/libravengun-bestand-log.so:$W/libravengun-fork-log.so:$W/libravengun-ptrace-log.so" "$LOADER" "$W/view/game"
EOF
chmod +x "$W/binnen.sh"
t0=$(date +%s); "${BW[@]}" /bin/bash "$W/binnen.sh" >"$W/log/B.out" 2>&1; rc=$?; t1=$(date +%s)
[ -n "$SOCAT" ] && kill "$SOCAT" 2>/dev/null

BEWAAR="$D/logs-koppel3-$(date +%Y%m%d-%H%M)"; mkdir -p "$BEWAAR"; cp "$W/log/"* "$BEWAAR/" 2>/dev/null
{
  echo "run            : koppel3-meting.sh (meting 2 + /dev/bus/usb terug)"
  echo "datum          : $(date -Is)"
  echo "exitcode       : $rc"
  echo "levensduur     : $((t1-t0)) s"
  echo "lader sha256   : $(sha256sum "$LOADER" | cut -d' ' -f1)"
  echo "gast sha256    : $sha"
  echo "kern sha256    : $(sha256sum "$KERN" | cut -d' ' -f1)"
  echo "kern doel      : $DOEL_KERN"
  echo "yama scope     : $(cat /proc/sys/kernel/yama/ptrace_scope)"
  echo "ENIGE wijziging t.o.v. meting 2: --dev-bind-try /dev/bus/usb /dev/bus/usb"
  echo "koppeling /g3  : --dir /g3 --ro-bind '$NASDIR' /g3"
  echo "koppeling /T4User: --overlay-src '$NASDIR/T4User' --overlay '$W/upper' '$W/work' /T4User"
  echo "grens          : NAS onaangeroerd (alle schrijfacties in de overlay-upper in /tmp);"
  echo "                 geen hostbestand, geen hostmount; alleen eigen PIDs opgeruimd"
  echo "bestand-regels : $( [ -f "$W/log/B.bestand.log" ] && wc -l < "$W/log/B.bestand.log" || echo 0 )"
  echo "fouten         : $( [ -f "$W/log/B.bestand.log" ] && grep -c FOUT "$W/log/B.bestand.log" || echo 0 )"
  echo "ATTACH ok      : $( [ -f "$W/log/B.ptrace.log" ] && grep -c 'PTRACE_ATTACH.*ok' "$W/log/B.ptrace.log" || echo 0 )"
} > "$BEWAAR/RESULT.txt" 2>&1
printf '  exit=%s  levensduur=%ss\n' "$rc" "$((t1-t0))"
sed 's/^/    /' "$BEWAAR/RESULT.txt"

# pad: het EERSTE quoted veld (een gretige regex pakt het laatste, en dat is sinds mode="rb" dus "rb")
pad(){ grep FOUT "$1" 2>/dev/null | sed -n 's/^[^"]*"\([^"]*\)".*/\1/p' | sort -u; }
nieuw="$BEWAAR/B.bestand.log"
kop "DE FOUTENLIJST, DRIE METINGEN NAAST ELKAAR"
if [ ! -s "$nieuw" ]; then
  warn "GEEN bestandslogboek -- niets vergeleken, geen uitspraak. Zie $BEWAAR/B.out"
else
  pad "$M1" > "$W/p1.txt"; pad "$M2" > "$W/p2.txt"; pad "$nieuw" > "$W/p3.txt"
  printf '  %-26s %-12s %-12s %-12s\n' "maat" "meting 1" "meting 2" "meting 3"
  printf '  %-26s %-12s %-12s %-12s\n' "FOUT-regels" "$(grep -c FOUT "$M1")" "$(grep -c FOUT "$M2")" "$(grep -c FOUT "$nieuw")"
  printf '  %-26s %-12s %-12s %-12s\n' "unieke paden" "$(wc -l < "$W/p1.txt")" "$(wc -l < "$W/p2.txt")" "$(wc -l < "$W/p3.txt")"
  printf '  %-26s %-12s %-12s %-12s\n' "bestandsregels" "$(wc -l < "$M1")" "$(wc -l < "$M2")" "$(wc -l < "$nieuw")"
  echo
  echo "  -- paden die in meting 1 EN 2 fout waren en NU weg zijn --"
  comm -12 "$W/p1.txt" "$W/p2.txt" > "$W/beide.txt"; comm -23 "$W/beide.txt" "$W/p3.txt" | sed 's/^/      /'
  echo "  -- nog steeds fout in alle drie --"; comm -12 "$W/beide.txt" "$W/p3.txt" | sed 's/^/      /'
  echo "  -- nieuw in meting 3 (zou leeg moeten zijn) --"; comm -13 "$W/beide.txt" "$W/p3.txt" | grep -v '^$' | sed 's/^/      /' || true
  echo "  -- als controle: alle fouten van meting 3, ruw --"
  grep FOUT "$nieuw" | grep -vE '"/tmp/ts-koppel3/log/' | sed 's/^/      /'
  echo
  echo "  -- TOETS OP MIJN VOORAF VASTGELEGDE VERWACHTING --"
  f3=$(grep -c FOUT "$nieuw"); r3=$(wc -l < "$nieuw")
  if [ "$f3" -le 9 ]; then ok "FOUT $f3: in de verwachte buurt van 8 (de drie USB-fouten zijn weg)"; else warn "FOUT $f3: NIET in de verwachte buurt van 8 -- mijn verklaring van meting 2 klopt dan niet"; fi
  if [ "$r3" -ge 80 ] && [ "$r3" -le 100 ]; then ok "regels $r3: terug in de buurt van de 89 van meting 1"; else warn "regels $r3: NIET terug bij ~89 -- er is meer veranderd dan ik dacht"; fi
fi

kop "DE GEPACEDE LUS (vierde keer)"
python3 "$D/kind-analyse.py" "$BEWAAR" 2>&1 | sed -n '/=== 1/,/=== 5/p' | sed 's/^/  /'
echo
echo "  -- naast de drie eerdere runs --"
printf '  %-22s %-12s %-12s %-12s %-12s\n' "maat" "meting1" "kinderen1" "meting2" "meting3"
printf '  %-22s %-12s %-12s %-12s %-12s\n' "ATTACH ok" \
  "$(grep -c 'PTRACE_ATTACH.*ok' "$D/logs-koppel-20261013-1201/B.ptrace.log")" \
  "$(grep -c 'kind-gemaakt' "$D/logs-koppel-20261013-1201/B.fork.log")" \
  "$(grep -c 'PTRACE_ATTACH.*ok' "$M2" 2>/dev/null || echo '-')" \
  "$( [ -f "$BEWAAR/B.ptrace.log" ] && grep -c 'PTRACE_ATTACH.*ok' "$BEWAAR/B.ptrace.log" || echo 0)"
printf '  %-22s %-12s %-12s %-12s %-12s\n' "kinderen" "388" "389" "$(grep -c 'PTRACE_ATTACH.*ok' "$M2" 2>/dev/null || echo '-')" "$( [ -f "$BEWAAR/B.fork.log" ] && grep -c 'kind-gemaakt' "$BEWAAR/B.fork.log" || echo 0)"
kop "BEWAARD"; echo "  $BEWAAR"; ls -la "$BEWAAR" | sed 's/^/    /'
