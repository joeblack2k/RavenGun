#!/bin/bash
# ts/142-venster-proef.sh - DE OPNAME REPAREREN EN HERHALEN.
#   Mijn oude lus stopte bij de EERSTE niet-lege PNG -- en de statusachtergrond staat er altijd, dus ik
#   nam de achtergrond en nooit het spelvenster. Nieuw:
#     * ik stop NIET meer vroeg: ik neem een reeks opnamen over de hele run;
#     * ik kijk tijdens de run of er een VENSTER van de gast bestaat (wmctrl/xdotool als die er zijn);
#     * ik noteer per opname de grootte, zodat "achtergrond" (klein, gelijk) van "spel" (groot) te scheiden is.
#   Verder identiek aan de vorige displayrun, met de koppelingen erin.
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
W=/tmp/ts-venster
DO_START=0; [ "${1:-}" = "--start" ] && DO_START=1
rm -rf "$W"; mkdir -p "$W"
ok(){ printf '  OK    %s\n' "$*"; }; warn(){ printf '  LET OP %s\n' "$*"; }; kop(){ printf '\n=== %s ===\n' "$*"; }

kop "1. WELKE VENSTER-HULPMIDDELEN ZIJN ER?"
for t in wmctrl xdotool kdotool qdbus6 qdbus; do
  p=$(command -v $t 2>/dev/null); [ -n "$p" ] && printf '  %-10s %s\n' "$t" "$p"
done
echo "  (wmctrl/xdotool zien alleen X11-vensters; de gast draait via Xwayland, dus dat zegt iets)"

for f in bestand-log.c ptracer-permit.c fork-log.c ptrace-log.c; do
  gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/libravengun-${f%.c}.so" "$D/$f" -ldl 2>&1 | head -3
done
"$TV" --check >/dev/null 2>&1 && ok "scherm bereikbaar" || { warn "geen scherm -- stop"; exit 1; }
bezet(){ local p; for p in gamescope gamescope-wl rg-elfloader wine wine64 BudgieLoader VACUUM.exe game; do
  pgrep -x "$p" >/dev/null 2>&1 && { printf '%s ' "$p"; return 0; }; done; return 1; }
if b=$(bezet); then warn "BEZET: $b -- ik start niet"; [ "$DO_START" = 1 ] && exit 1; else ok "bench vrij"; fi
printf '  lader: %s\n' "$(sha256sum "$LOADER" | cut -d' ' -f1)"
if [ "$DO_START" != 1 ]; then kop "KLAAR"; exit 0; fi

kop "2. DE RUN MET DE KOPPELINGEN EN EEN OPNAME-REEKS"
w="$W/run"; mkdir -p "$w/log" "$w/upper" "$w/work" "$w/view"
cp "$GAME" "$w/view/game"
for d in T4Data ServiceDongleApp hasp env irgun GeneralPurposeStateMachine.txt; do
  [ -e "$NASDIR/$d" ] && ln -sfn "$NASDIR/$d" "$w/view/$d"
done
[ -e "$NASDIR/T4User" ] && cp -r "$NASDIR/T4User" "$w/view/T4User"
cp "$NASDIR/T4User/log/syslog_bb3.txt" "$w/view/syslog_bb3.txt" 2>/dev/null
socat -d -d pty,raw,echo=0,link="$w/sierra2" pty,raw,echo=0,link="$w/peer" >"$w/socat.log" 2>&1 &
SOC=$!; for i in $(seq 1 20); do [ -L "$w/sierra2" ] && break; sleep 0.2; done

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
exec timeout 45 env LD_PRELOAD="$W/libravengun-ptracer-permit.so:$W/libravengun-bestand-log.so:$W/libravengun-fork-log.so:$W/libravengun-ptrace-log.so" "$LOADER" "$w/view/game" -n -f1360x768
EOF
t0=$(date +%s)
"$TV" -- /bin/bash "$w/binnen.sh" >"$w/log/B.out" 2>&1 &
bg=$!
# geen enkele break meer: acht opnamen over de run, en per opname het vensterbeeld
for i in 1 2 3 4 5 6 7 8; do
  sleep 0.45
  "$TV" -- spectacle -b -n -f -o "$w/shot-$i.png" >/dev/null 2>&1 || true
  if command -v wmctrl >/dev/null 2>&1; then
    "$TV" -- wmctrl -l 2>/dev/null | sed "s/^/VENSTER $i: /" >>"$w/vensters.txt"
  elif command -v xdotool >/dev/null 2>&1; then
    "$TV" -- xdotool search --name . getwindowname %@ 2>/dev/null | head -8 | sed "s/^/VENSTER $i: /" >>"$w/vensters.txt"
  fi
done
wait $bg 2>/dev/null; rc=$?; t1=$(date +%s); kill "$SOC" 2>/dev/null
printf '  exit=%s  levensduur=%ss  regels=%s  FOUT=%s\n' "$rc" "$((t1-t0))" \
  "$(wc -l < "$w/log/B.bestand.log" 2>/dev/null || echo 0)" "$(grep -c FOUT "$w/log/B.bestand.log" 2>/dev/null || echo 0)"
echo "  -- de opnamen, met groottes (achtergrond is klein en gelijk, spel is groot) --"
for s in "$w"/shot-*.png; do [ -f "$s" ] && printf '    %-12s %9s B\n' "$(basename "$s")" "$(stat -c%s "$s")"; done
echo "  -- de vensterlijst tijdens de run --"
if [ -s "$w/vensters.txt" ]; then grep -iE 'game|terminator|rg-elfloader' "$w/vensters.txt" | head -6 | sed 's/^/    /'; echo "    (en totaal $(wc -l < "$w/vensters.txt") regels)"; else echo "    (geen vensterhulpmiddel of geen uitvoer)"; fi
echo "  -- de foutenlijst (koppelingen nu werk?) --"
grep FOUT "$w/log/B.bestand.log" 2>/dev/null | sed -n 's/.*"\([^"]*\)".*/    \1/p' | sort | uniq -c | sort -rn
BEW="$D/logs-venster-$(date +%Y%m%d-%H%M)"; mkdir -p "$BEW"; cp "$w/log/"* "$BEW/" 2>/dev/null; cp "$w"/shot-*.png "$BEW/" 2>/dev/null; cp "$w/vensters.txt" "$BEW/" 2>/dev/null
{ echo "run: vensterproef (opname-reeks, koppelingen, argumenten)"; echo "datum: $(date -Is)"
  echo "exitcode: $rc"; echo "levensduur: $((t1-t0)) s"; echo "FOUT: $(grep -c FOUT "$w/log/B.bestand.log")"
  echo "-- opnamen --"; for s in "$BEW"/shot-*.png; do [ -f "$s" ] && echo "$(basename "$s") $(stat -c%s "$s")"; done; } > "$BEW/RESULT.txt"
echo "  bewaard: $BEW"
kop "3. OPRUIMEN (alleen eigen PIDs)"; rm -rf "$w/view" "$w/log" 2>/dev/null
for p in /g3 /T4User; do [ -e "$p" ] && echo "  LET OP: $p bestaat" || echo "  $p bestaat niet (goed)"; done
