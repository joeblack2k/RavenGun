#!/bin/bash
# koppel4-meting.sh - DE ARGUMENTEN-PROEF: dezelfde opzet als meting 3, met EEN variabele erbij:
# het spel krijgt de argumenten die het project zelf voorschrijft, `-n -f1360x768`.
#
#   bash koppel4-meting.sh            # voorbereiding + proef (bench-vrij). START NIETS.
#   bash koppel4-meting.sh --start    # de run (max 90 s). START DE GAME.
#
# VOORAF VASTGELEGD WAT IK VERWACHT:
#   * de lader hoort nu argc 3 door te geven (game, -n, -f1360x768) -- dat lees ik terug in het laderlogboek,
#     dus ik kan CONTROLEREN dat de variabele echt is aangekomen;
#   * ofwel het gedrag verandert (meer/minder bestandsregels, andere fouten, een andere lus, of beeld),
#     ofwel het blijft identiek en dan is dat een GEMETEN NULRESULTAAT over de argumenten;
#   * RISICO dat ik vooraf benoem: `-f1360x768` vraagt volledig scherm, en in mijn sandbox is er GEEN
#     display (geen gamescope). Als de gast daardoor juist VROEGER afhaakt, dan betekent dat "de argumenten
#     veranderen de weg", niet "de argumenten breken het" -- de vergelijking met meting 3 is dan minder
#     schoon en dat zeg ik dan ook.
# Verder is alles identiek aan meting 3 (koppeling /g3 + /T4User-overlay, /dev/bus/usb, synthetische
# /dev/sierra2, de kopie van syslog_bb3.txt in de werkmap).
set -u
R="$HOME/projects/ravengun"
LOADER="$R/tools/rg-elfloader/rg-elfloader"
NASDIR="/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation "
GAME="$NASDIR/game"
EL="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2"
LB32="$HOME/.local/share/ravengun/runtime/lib32"
D="$HOME/projects/ravengun-patches/patches/Terminator"
INC="$R/tools/rg-elfloader/inc"
W=/tmp/ts-args
DO_START=0; [ "${1:-}" = "--start" ] && DO_START=1
ARGUMENTEN=(-n -f1360x768)
BASIS="$D/logs-koppel3-20261013-1335/B.bestand.log"
rm -rf "$W"; mkdir -p "$W/log" "$W/upper" "$W/work" "$W/view"
ok(){ printf '  OK    %s\n' "$*"; }; warn(){ printf '  LET OP %s\n' "$*"; }; kop(){ printf '\n=== %s ===\n' "$*"; }
SOCAT=""
bezet(){ local p; for p in gamescope gamescope-wl rg-elfloader wine wine64 BudgieLoader VACUUM.exe game; do
  pgrep -x "$p" >/dev/null 2>&1 && { printf '%s ' "$p"; return 0; }; done; return 1; }

kop "VOORBEREIDING"
if b=$(bezet); then warn "BEZET door: $b -- ik start NIET"; [ "$DO_START" = 1 ] && exit 1; else ok "bench vrij"; fi
printf '  lader: %s\n' "$(sha256sum "$LOADER" | cut -d' ' -f1)"
for f in bestand-log.c ptracer-permit.c fork-log.c ptrace-log.c; do
  gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/libravengun-${f%.c}.so" "$D/$f" -ldl 2>&1 | head -3
done
cp "$GAME" "$W/view/game"
for d in T4Data ServiceDongleApp hasp env irgun GeneralPurposeStateMachine.txt; do
  [ -e "$NASDIR/$d" ] && ln -sfn "$NASDIR/$d" "$W/view/$d"
done
[ -e "$NASDIR/T4User" ] && cp -r "$NASDIR/T4User" "$W/view/T4User"
cp "$NASDIR/T4User/log/syslog_bb3.txt" "$W/view/syslog_bb3.txt" 2>/dev/null && ok "syslog_bb3.txt in de werkmap"
if command -v socat >/dev/null 2>&1; then
  socat -d -d pty,raw,echo=0,link="$W/sierra2" pty,raw,echo=0,link="$W/sierra2-peer" >"$W/socat.log" 2>&1 &
  SOCAT=$!; for i in $(seq 1 20); do [ -L "$W/sierra2" ] && break; sleep 0.2; done
  [ -L "$W/sierra2" ] && ok "PTY voor /dev/sierra2 (eigen socat-pid $SOCAT)"
fi
BW=(bwrap --die-with-parent
    --ro-bind /usr /usr
    --symlink usr/lib /lib --symlink usr/lib64 /lib64 --symlink usr/bin /bin --symlink usr/bin /sbin
    --ro-bind /etc /etc --proc /proc --ro-bind /sys /sys
    --ro-bind /home /home --ro-bind /var /var --ro-bind /opt /opt --ro-bind /boot /boot --ro-bind /srv /srv)
[ -e /root ] && BW+=(--ro-bind /root /root)
BW+=(--dev /dev --dev-bind-try /dev/pts /dev/pts --dev-bind-try /dev/bus/usb /dev/bus/usb)
for dn in null zero full random urandom tty shm dri snd ttyS0 input; do
  [ -e "/dev/$dn" ] && BW+=(--dev-bind-try "/dev/$dn" "/dev/$dn")
done
[ -L "$W/sierra2" ] && [ -n "$SOCAT" ] && BW+=(--dev-bind "$(readlink -f "$W/sierra2")" /dev/sierra2)
BW+=(--ro-bind /run /run --bind-try /run/user/1000 /run/user/1000 --bind /tmp /tmp)
BW+=(--dir /g3 --ro-bind "$NASDIR" /g3)
BW+=(--overlay-src "$NASDIR/T4User" --overlay "$W/upper" "$W/work" /T4User)

if [ "$DO_START" != 1 ]; then kop "KLAAR"; echo "  Draai met: bash $0 --start"; [ -n "$SOCAT" ] && kill "$SOCAT" 2>/dev/null; exit 0; fi

kop "DE RUN MET ARGUMENTEN: ${ARGUMENTEN[*]}"
cat >"$W/binnen.sh" <<EOF
cd "$W/view" || exit 1
export LD_LIBRARY_PATH="$EL/libs:$LB32"
export RG_ELFLOADER_VERBOSE=1
export RAVENGUN_BESTAND_LOG="$W/log/B.bestand.log"
export RAVENGUN_FORK_LOG="$W/log/B.fork.log"
export RAVENGUN_PTRACE_LOG="$W/log/B.ptrace.log"
export RAVENGUN_PTRACER_LOG="$W/log/B.permit.log"
exec timeout 90 env LD_PRELOAD="$W/libravengun-ptracer-permit.so:$W/libravengun-bestand-log.so:$W/libravengun-fork-log.so:$W/libravengun-ptrace-log.so" "$LOADER" "$W/view/game" ${ARGUMENTEN[*]}
EOF
t0=$(date +%s); "${BW[@]}" /bin/bash "$W/binnen.sh" >"$W/log/B.out" 2>&1; rc=$?; t1=$(date +%s)
[ -n "$SOCAT" ] && kill "$SOCAT" 2>/dev/null
BEW="$D/logs-argumenten-$(date +%Y%m%d-%H%M)"; mkdir -p "$BEW"; cp "$W/log/"* "$BEW/" 2>/dev/null
{ echo "proef          : argumenten ${ARGUMENTEN[*]} (een variabele t.o.v. meting 3)"
  echo "datum          : $(date -Is)"; echo "exitcode       : $rc"; echo "levensduur     : $((t1-t0)) s"
  echo "lader sha256   : $(sha256sum "$LOADER" | cut -d' ' -f1)"; echo "gast sha256    : $(sha256sum "$GAME" | cut -d' ' -f1)"
  echo "kern sha256    : $(sha256sum "$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/TeknoParrot/TeknoParrot64.dll" | cut -d' ' -f1)"
  echo "grens          : NAS onaangeroerd (overlay-upper in /tmp); alleen eigen PIDs opgeruimd"
  echo "regels         : $(wc -l < "$W/log/B.bestand.log" 2>/dev/null || echo 0)"
  echo "FOUT           : $(grep -c FOUT "$W/log/B.bestand.log" 2>/dev/null || echo 0)"
  echo "ATTACH ok      : $(grep -c 'PTRACE_ATTACH.*ok' "$W/log/B.ptrace.log" 2>/dev/null || echo 0)"
} > "$BEW/RESULT.txt"
printf '  exit=%s  levensduur=%ss\n' "$rc" "$((t1-t0))"; sed 's/^/    /' "$BEW/RESULT.txt"

kop "1. IS DE VARIABELE AANGEKOMEN? (argc en de argumenten in het laderlogboek)"
grep -nE 'argc|argv\[0\]|spring naar e_entry' "$W/log/B.out" | head -5 | sed 's/^/  /'
a=$(grep -o 'argc [0-9]*' "$W/log/B.out" | head -1)
printf '  %s  (verwacht: argc 3)\n' "${a:-geen argc-regel gevonden}"
kop "2. VERGELIJKING MET METING 3 (zonder argumenten)"
if [ ! -s "$W/log/B.bestand.log" ]; then
  warn "GEEN bestandslogboek -- geen uitspraak mogelijk. Zie $BEW/B.out"; sed 's/^/    /' "$W/log/B.out" | head -8
else
  pad(){ grep FOUT "$1" 2>/dev/null | sed -n 's/^[^"]*"\([^"]*\)".*/\1/p' | sort -u; }
  pad "$BASIS" > "$W/p0.txt"; pad "$W/log/B.bestand.log" > "$W/p1.txt"
  printf '  %-22s %-12s %-12s\n' "maat" "meting 3" "met argumenten"
  printf '  %-22s %-12s %-12s\n' "bestandsregels" "$(wc -l < "$BASIS")" "$(wc -l < "$W/log/B.bestand.log")"
  printf '  %-22s %-12s %-12s\n' "FOUT-regels" "$(grep -c FOUT "$BASIS")" "$(grep -c FOUT "$W/log/B.bestand.log")"
  printf '  %-22s %-12s %-12s\n' "ATTACH ok" "$(grep -c 'PTRACE_ATTACH.*ok' "$D/logs-koppel3-20261013-1335/B.ptrace.log")" "$(grep -c 'PTRACE_ATTACH.*ok' "$W/log/B.ptrace.log")"
  printf '  %-22s %-12s %-12s\n' "kinderen" "$(grep -c 'kind-gemaakt' "$D/logs-koppel3-20261013-1335/B.fork.log")" "$(grep -c 'kind-gemaakt' "$W/log/B.fork.log")"
  echo "  -- nieuw t.o.v. meting 3 --"; comm -13 "$W/p0.txt" "$W/p1.txt" | sed 's/^/      /'
  echo "  -- verdwenen --"; comm -23 "$W/p0.txt" "$W/p1.txt" | sed 's/^/      /'
  echo "  -- alle fouten nu --"; grep FOUT "$W/log/B.bestand.log" | grep -vE '"/tmp/ts-args/log/' | sed 's/^/      /'
fi
kop "3. DE LUS"
python3 "$D/kind-analyse.py" "$BEW" 2>&1 | sed -n '/=== 1/,/=== 5/p' | sed 's/^/  /'
kop "4. OPRUIMEN (alleen eigen PIDs) en grens"; rm -rf "$W/view" "$W/log"
for p in /g3 /T4User; do [ -e "$p" ] && echo "  LET OP: $p bestaat" || echo "  $p bestaat niet (goed)"; done
printf '  kern: %s\n' "$(sha256sum "$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/TeknoParrot/TeknoParrot64.dll" | cut -d' ' -f1 | cut -c1-16)"
echo "  bewaard: $BEW"
