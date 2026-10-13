#!/bin/bash
# ts/148-vensterlijst.sh - DE VRAAG VOORAF: BESTAAT HET SPELVENSTER UEBERHAUPT?
#   Niet "tekent het spel" maar "is er een venster". Mijn opname legt de SAMENGESTELDE laag vast, dus een
#   spelvenster dat achter de desktop ligt zie ik daar niet in -- maar wel in de vensterlijst.
#
#   Wat ik per ronde vastleg:
#     * een opname (de samengestelde laag);
#     * de VOLLEDIGE X11-vensterlijst met namen (xdotool search --name . getwindowname);
#     * de actieve vensternaam (wat heeft de focus);
#     * de geometrie van elk venster (om te zien of iets het hele scherm bedekt).
#   Bord staat aan zoals het nu is -- dat is de referentie van vóór 13:54 niet, maar wel de huidige staat.
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
W=/tmp/ts-vensterlijst
DO_START=0; [ "${1:-}" = "--start" ] && DO_START=1
rm -rf "$W"; mkdir -p "$W"
ok(){ printf '  OK    %s\n' "$*"; }; warn(){ printf '  LET OP %s\n' "$*"; }; kop(){ printf '\n=== %s ===\n' "$*"; }

kop "VOORAF VASTGELEGD"
echo "  VRAAG: bestaat er tijdens de run een X11-venster van het spel?"
echo "  JA  -> dan tekent het spel wel maar ligt de desktop eroverheen (stapelprobleem, met een oplossing)"
echo "  NEE -> dan maakt het spel in deze runs geen venster aan, en zit het verschil met 13:54 in de START"

for f in bestand-log.c ptracer-permit.c fork-log.c ptrace-log.c; do
  gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/libravengun-${f%.c}.so" "$D/$f" -ldl 2>&1 | head -3
done
"$TV" --check >/dev/null 2>&1 && ok "scherm bereikbaar" || { warn "geen scherm"; exit 1; }
bezet(){ local p; for p in gamescope gamescope-wl rg-elfloader wine wine64 BudgieLoader VACUUM.exe game; do
  pgrep -x "$p" >/dev/null 2>&1 && { printf '%s ' "$p"; return 0; }; done; return 1; }
if b=$(bezet); then warn "BEZET: $b"; [ "$DO_START" = 1 ] && exit 1; else ok "bench vrij"; fi
printf '  bord: view=%s saver=%s (dat is nu de bedoelde toestand)\n' \
  "$(systemctl --user is-active ravengun-oled-view.service 2>&1)" \
  "$(systemctl --user is-active ravengun-oled-saver.service 2>&1)"
echo "  -- de vensterlijst VOOR de run (nulmeting) --"
"$TV" -- xdotool search --name . getwindowname %@ 2>/dev/null | sort -u | head -20 | sed 's/^/    /'
if [ "$DO_START" != 1 ]; then kop "KLAAR"; exit 0; fi

kop "DE RUN"
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
for i in 1 2 3 4 5 6; do
  sleep 0.6
  "$TV" -- spectacle -b -n -f -o "$w/shot-$i.png" >/dev/null 2>&1 || true
  printf -- "--- ronde %s ---\n" "$i" >>"$w/vensters.txt"
  "$TV" -- xdotool search --name . getwindowname %@ 2>/dev/null | sort -u >>"$w/vensters.txt"
  printf 'actief: %s\n' "$("$TV" -- xdotool getactivewindow getwindowname 2>/dev/null || echo '?')" >>"$w/vensters.txt"
  "$TV" -- xdotool search --name . getwindowgeometry %@ 2>/dev/null | head -30 >>"$w/geometrie.txt"
done
wait $bg 2>/dev/null; rc=$?; t1=$(date +%s); kill "$SOC" 2>/dev/null
printf '  exit=%s  levensduur=%ss  regels=%s  FOUT=%s\n' "$rc" "$((t1-t0))" \
  "$(wc -l < "$w/log/B.bestand.log" 2>/dev/null || echo 0)" "$(grep -c FOUT "$w/log/B.bestand.log" 2>/dev/null || echo 0)"
echo "  -- opnamen met hash --"
for s in "$w"/shot-*.png; do [ -f "$s" ] && printf '    %-12s %9s B  %s\n' "$(basename "$s")" "$(stat -c%s "$s")" "$(sha256sum "$s" | cut -c1-12)"; done

kop "HET ANTWOORD OP DE VRAAG: bestond er een spelvenster?"
echo "  -- de volledige vensterlijst tijdens de run (unieke namen) --"
sort -u "$w/vensters.txt" | grep -v '^--- ronde' | grep -v '^$' | head -25 | sed 's/^/    /'
n=$(sort -u "$w/vensters.txt" | grep -ciE 'terminator|salvation|rg-elfloader|^game$' || true)
printf '  vensters met een spelnaam erin: %s\n' "$n"
if [ "$n" -gt 0 ]; then ok "JA: er BESTAAT een spelvenster -> het is een stapel-/focusprobleem"
else warn "NEE: geen spelvenster in de X11-lijst -> het spel maakt er in deze runs geen aan"; fi
echo "  -- wat had de focus, per ronde? --"
grep '^actief:' "$w/vensters.txt" | sed 's/^/    /'
BEW="$D/logs-vensterlijst-$(date +%Y%m%d-%H%M)"; mkdir -p "$BEW"
cp "$w/log/"* "$BEW/" 2>/dev/null; cp "$w"/shot-*.png "$BEW/" 2>/dev/null
cp "$w/vensters.txt" "$w/geometrie.txt" "$BEW/" 2>/dev/null
{ echo "run: vensterlijst-proef (bestaat het spelvenster?)"; echo "datum: $(date -Is)"
  echo "exitcode: $rc"; echo "levensduur: $((t1-t0)) s"; echo "FOUT: $(grep -c FOUT "$w/log/B.bestand.log")"
  echo "spelvensters gevonden: $n"
  echo "-- opnamen --"; for s in "$BEW"/shot-*.png; do [ -f "$s" ] && echo "$(basename "$s") $(stat -c%s "$s") $(sha256sum "$s" | cut -c1-12)"; done
  echo "-- vensters --"; sort -u "$BEW/vensters.txt" | grep -v '^--- ronde'; } > "$BEW/RESULT.txt"
echo "  bewaard: $BEW"
kop "OPRUIMEN (alleen eigen PIDs)"; rm -rf "$w/view" "$w/log" 2>/dev/null
for p in /g3 /T4User; do [ -e "$p" ] && echo "  LET OP: $p bestaat" || echo "  $p bestaat niet (goed)"; done
