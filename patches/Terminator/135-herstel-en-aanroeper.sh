#!/bin/bash
# ts/135-herstel-en-aanroeper.sh
#   1. BENCH-VRIJ: bewijzen dat de nieuwe fork-log WIE exit() aanroept logt (op een proefproces).
#   2. DE HERSTELDE DISPLAYRUN: de koppelingen terug (/T4User-overlay, syslog_bt3.txt-kopie, socat-PTY),
#      met scherm en met argumenten. EEN variabele t.o.v. de vorige displayrun: de koppelingen.
#      VERWACHTING VOORAF: FOUT terug naar 8 (niet 21) en HET BEELD BLIJFT.
#   3. De aanroeper-keten omzetten in een ADRES + OFFSET in de gast, en de code eromheen disassembleren.
set -u
R="$HOME/projects/ravengun"
LOADER="$R/tools/rg-elfloader/rg-elfloader"
NASDIR="/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation "
GAME="$NASDIR/game"
EL="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2"
LB32="$HOME/.local/share/ravengun/runtime/lib32"
D="$HOME/projects/ravengun-patches/patches/Terminator"
TV="$R/scripts/rg-tv-env.sh"
INC="$R/tools/rg-elfloader/inc"
W=/tmp/ts-herstel
DO_START=0; [ "${1:-}" = "--start" ] && DO_START=1
ARGUMENTEN=(-n -f1360x768)
rm -rf "$W"; mkdir -p "$W"
ok(){ printf '  OK    %s\n' "$*"; }; warn(){ printf '  LET OP %s\n' "$*"; }; kop(){ printf '\n=== %s ===\n' "$*"; }
bezet(){ local p; for p in gamescope gamescope-wl rg-elfloader wine wine64 BudgieLoader VACUUM.exe game; do
  pgrep -x "$p" >/dev/null 2>&1 && { printf '%s ' "$p"; return 0; }; done; return 1; }

kop "1. DE NIEUWE fork-log BOUWEN EN BENCH-VRIJ BEPROEVEN"
gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/libravengun-fork-log.so" "$D/fork-log.c" -ldl 2>&1 | head -6
if [ -s "$W/libravengun-fork-log.so" ]; then ok "gebouwd"; else warn "NIET gebouwd"; exit 1; fi
cat >"$W/p.c" <<'EOF'
#include <stdlib.h>
int main(void){ exit(7); }
EOF
gcc -m32 -pie -fPIE -O1 -I"$INC" -o "$W/p" "$W/p.c" 2>&1 | head -3
rm -f "$W/proef.log"
RAVENGUN_FORK_LOG="$W/proef.log" LD_PRELOAD="$W/libravengun-fork-log.so" "$W/p" >/dev/null 2>&1
echo "  -- wat de waarnemer schrijft --"
grep 'einde' "$W/proef.log" 2>/dev/null | sed 's/^/    /'
grep -q 'aanroeper- keten' "$W/proef.log" && ok "de aanroeper-keten WORDT gelogd" || warn "geen aanroeper-keten in het logboek"

for f in bestand-log.c ptracer-permit.c ptrace-log.c; do
  gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/libravengun-${f%.c}.so" "$D/$f" -ldl 2>&1 | head -3
done

kop "2. SCHERMCONTROLE"
"$TV" --check 2>&1 | head -3 | sed 's/^/  /' || { warn "geen bereikbaar scherm -- ik stop"; exit 1; }
if b=$(bezet); then warn "BEZET door: $b -- ik start NIET"; [ "$DO_START" = 1 ] && exit 1; else ok "bench vrij"; fi
printf '  lader: %s\n' "$(sha256sum "$LOADER" | cut -d' ' -f1)"
if [ "$DO_START" != 1 ]; then kop "KLAAR"; echo "  Draai met: bash $0 --start"; exit 0; fi

kop "3. DE HERSTELDE DISPLAYRUN (koppelingen terug, scherm, argumenten)"
w="$W/run"; mkdir -p "$w/log" "$w/upper" "$w/work" "$w/view"
cp "$GAME" "$w/view/game"
for d in T4Data ServiceDongleApp hasp env irgun GeneralPurposeStateMachine.txt; do
  [ -e "$NASDIR/$d" ] && ln -sfn "$NASDIR/$d" "$w/view/$d"
done
[ -e "$NASDIR/T4User" ] && cp -r "$NASDIR/T4User" "$w/view/T4User"
# de koppelingen die de vorige displayrun kwijt was:
cp "$NASDIR/T4User/log/syslog_bb3.txt" "$w/view/syslog_bb3.txt" 2>/dev/null && ok "syslog_bb3.txt-kopie terug ($(stat -c%s "$w/view/syslog_bb3.txt") B)"
socat -d -d pty,raw,echo=0,link="$w/sierra2" pty,raw,echo=0,link="$w/peer" >"$w/socat.log" 2>&1 &
SOC=$!; for i in $(seq 1 20); do [ -L "$w/sierra2" ] && break; sleep 0.2; done
[ -L "$w/sierra2" ] && ok "socat-PTY terug voor /dev/sierra2 (eigen pid $SOC)"

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
[ -L "$w/sierra2" ] && BW+=(--dev-bind "$(readlink -f "$w/sierra2")" /dev/sierra2)
BW+=(--ro-bind /run /run --bind-try /run/user/1000 /run/user/1000 --bind /tmp /tmp)
BW+=(--dir /g3 --ro-bind "$NASDIR" /g3)
BW+=(--overlay-src "$NASDIR/T4User" --overlay "$w/upper" "$w/work" /T4User)

cat >"$w/binnen.sh" <<EOF
cd "$w/view" || exit 1
export LD_LIBRARY_PATH="$EL/libs:$LB32"
export RG_ELFLOADER_VERBOSE=1
export RAVENGUN_BESTAND_LOG="$w/log/B.bestand.log"
export RAVENGUN_FORK_LOG="$w/log/B.fork.log"
export RAVENGUN_PTRACE_LOG="$w/log/B.ptrace.log"
export RAVENGUN_PTRACER_LOG="$w/log/B.permit.log"
exec timeout 45 env LD_PRELOAD="$W/libravengun-ptracer-permit.so:$W/libravengun-bestand-log.so:$W/libravengun-fork-log.so:$W/libravengun-ptrace-log.so" "$LOADER" "$w/view/game" ${ARGUMENTEN[*]}
EOF
t0=$(date +%s)
"$TV" -- /bin/bash "$w/binnen.sh" >"$w/log/B.out" 2>&1 &
bg=$!
for i in 1 2 3 4 5; do sleep 0.8; "$TV" -- spectacle -b -n -f -o "$w/shot-$i.png" >/dev/null 2>&1 || true; [ -s "$w/shot-$i.png" ] && break; done
wait $bg 2>/dev/null; rc=$?; t1=$(date +%s); kill "$SOC" 2>/dev/null
printf '  exit=%s  levensduur=%ss  regels=%s  FOUT=%s  ATTACH=%s\n' "$rc" "$((t1-t0))" \
  "$(wc -l < "$w/log/B.bestand.log" 2>/dev/null || echo 0)" "$(grep -c FOUT "$w/log/B.bestand.log" 2>/dev/null || echo 0)" \
  "$(grep -c 'PTRACE_ATTACH.*ok' "$w/log/B.ptrace.log" 2>/dev/null || echo 0)"
echo "  -- TOETS OP DE VERWACHTING: FOUT 8 (was 21) en beeld blijft --"
f=$(grep -c FOUT "$w/log/B.bestand.log" 2>/dev/null || echo 99)
[ "$f" = "8" ] && ok "FOUT = 8: de koppelingen zijn terug, en er is GEEN nieuwe fout" || warn "FOUT = $f (verwacht 8)"
s=$(stat -c%s "$w/shot-1.png" 2>/dev/null || echo 0)
[ "$s" -gt 500000 ] && ok "opname is $s B -> het beeld is er nog" || warn "opname $s B -> mogelijk geen beeld"
echo "  -- en de foutenlijst --"; grep FOUT "$w/log/B.bestand.log" | sed 's/^/    /'
BEW="$D/logs-herstel-$(date +%Y%m%d-%H%M)"; mkdir -p "$BEW"; cp "$w/log/"* "$BEW/" 2>/dev/null; cp "$w"/shot-*.png "$BEW/" 2>/dev/null
{ echo "run: herstelde displayrun (koppelingen terug, scherm, argumenten)"; echo "datum: $(date -Is)"
  echo "exitcode: $rc"; echo "levensduur: $((t1-t0)) s"; echo "regels: $(wc -l < "$w/log/B.bestand.log")"
  echo "FOUT: $(grep -c FOUT "$w/log/B.bestand.log")"; echo "opname: $(stat -c%s "$w/shot-1.png" 2>/dev/null || echo 0) B"; } > "$BEW/RESULT.txt"
echo "  bewaard: $BEW"

kop "4. WIE RIEP exit() AAN? (adres + offset in de gast, en de code eromheen)"
grep -o 'einde exit-code=[0-9-]* aanroeper- keten:.*' "$w/log/B.fork.log" | tail -2 | sed 's/^/  /'
ADR=$(grep -o 'aanroeper- keten: *0x[0-9a-f]*' "$w/log/B.fork.log" | tail -1 | grep -o '0x[0-9a-f]*')
if [ -n "$ADR" ]; then
  printf '  eerste adres in de keten: %s\n' "$ADR"
  python3 -c "a=int('$ADR',16); print('  offset t.o.v. de laadbasis 0x08048000: 0x%x' % (a-0x08048000))"
  printf '  -- wat staat daar? (objdump rond dat adres) --\n'
  objdump -d --start-address=$(( ADR - 0x60 )) --stop-address=$(( ADR + 0x20 )) "$GAME" 2>/dev/null \
    | sed -n '/^ *[0-9a-f]* </,$p' | tail -28 | sed 's/^/    /'
  printf '  -- welk symbool hoort erbij? (dichtstbijzijnde symbolen) --\n'
  nm -C "$GAME" 2>/dev/null | sort | awk -v a=$(( ADR )) '
    { adres=strtonum("0x" $1); if (adres<=a) vorige=$0 } END { print "    laatste symbool voor het adres:", vorige }'
  nm -D "$GAME" 2>/dev/null | sort | awk -v a=$(( ADR )) '
    { adres=strtonum("0x" $1); if (adres<=a) vorige=$0 } END { print "    laatste DYN-symbool:", vorige }'
else
  warn "geen adres in de keten gevonden"
fi

kop "5. OPRUIMEN (alleen eigen PIDs) en grens"; rm -rf "$w/view" "$w/log" 2>/dev/null; rm -rf "$W" 2>/dev/null
for p in /g3 /T4User; do [ -e "$p" ] && echo "  LET OP: $p bestaat" || echo "  $p bestaat niet (goed)"; done
printf '  kern: %s\n' "$(sha256sum "$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/TeknoParrot/TeknoParrot64.dll" | cut -d' ' -f1 | cut -c1-16)"
