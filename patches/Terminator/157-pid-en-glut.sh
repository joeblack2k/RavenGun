#!/bin/bash
# ts/157-pid-en-glut.sh
#   1. BENCH-VRIJ: wat was pid 1014133 (= appid "steam_proton" dat van 13:44:39 tot 13:56:05 de voorgrond
#      had)? Was dat een Terminator Salvation onder Proton, of iets anders? Dat bepaalt of mijn
#      13:54-opnamen van DEZE titel waren.
#   2. De GLUT-proef: wordt glutCreateWindow aangeroepen, en wat geeft hij terug?
#      VOORAF: niet aangeroepen -> geen venster, geen displayprobleem (gast-tak). Wel aangeroepen ->
#      retourwaarde + errno zijn het antwoord. En glutSwapBuffers is de rookproef voor "tekent hij".
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
W=/tmp/ts-glut
DO_START=0; [ "${1:-}" = "--start" ] && DO_START=1
rm -rf "$W"; mkdir -p "$W"
kop(){ printf '\n=== %s ===\n' "$*"; }

kop "1. WAT WAS PID 1014133? (bench-vrij)"
echo "  -- de focusregel die we al hebben --"
journalctl --user --since '2026-10-13 13:40' --until '2026-10-13 13:57' --no-pager 2>/dev/null \
  | grep -i '1014133' | head -10 | sed 's/^/    /'
echo "  -- en alles rond die pid in het journal (scope, app, steam) --"
journalctl --user --since '2026-10-13 13:43' --until '2026-10-13 13:47' --no-pager 2>/dev/null \
  | grep -iE '1014133|app-|steam|proton|game|salvation|terminator' | head -20 | sed 's/^/    /'
echo "  -- leeft die pid nog? --"
ps -p 1014133 -o pid,ppid,etime,cmd 2>/dev/null | sed 's/^/    /' || echo "    (pid bestaat niet meer)"
echo "  -- en wat draait er NU van steam/proton? --"
pgrep -af 'proton|pressure|wine|steamapps' 2>/dev/null | head -8 | sed 's/^/    /' || echo "    niets"
echo "  -- de steam-appid-registers: welk spel is dat? --"
for f in "$HOME/.steam/steam/logs/content_log.txt" "$HOME/.steam/steam/logs/console-linux.txt"; do
  [ -f "$f" ] && { echo "    $f:"; grep -iE 'terminator|salvation' "$f" 2>/dev/null | tail -4 | sed 's/^/      /'; }
done
echo "  -- en de compatdata-mappen (welke appids zijn Proton-games geweest) --"
ls -1 "$HOME/.steam/steam/steamapps/compatdata" 2>/dev/null | head -12 | sed 's/^/    appid /'
for d in "$HOME/.steam/steam/steamapps/compatdata"/*; do
  [ -d "$d" ] || continue
  n=$(basename "$d")
  [ -f "$d/version" ] && printf '    appid %-10s mtime %s\n' "$n" "$(stat -c '%y' "$d" | cut -c1-19)"
done 2>/dev/null | head -12

for f in glut-log.c bestand-log.c ptracer-permit.c fork-log.c ptrace-log.c; do
  gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/libravengun-${f%.c}.so" "$D/$f" -ldl 2>&1 | head -3
done
[ -s "$W/libravengun-glut-log.so" ] && echo "  OK: de GLUT-wikkel is gebouwd" || { echo "  FOUT: GLUT-wikkel niet gebouwd"; exit 1; }
echo "  -- wat de wikkel exporteert --"
nm -D --defined-only "$W/libravengun-glut-log.so" 2>/dev/null | awk '{print "    " $3}' | sort | head -12
echo "  -- en welke glut-functies importeert de gast? --"
readelf -sW --dyn-syms "$GAME" 2>/dev/null | awk '$7=="UND"' | grep -oE 'glut[A-Za-z]*' | sort -u | sed 's/^/    /'
if [ "$DO_START" != 1 ]; then kop "KLAAR"; exit 0; fi

kop "2. DE GLUT-PROEF: een run met de wikkel erbij"
"$TV" --check >/dev/null 2>&1 || { echo "  geen scherm"; exit 1; }
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
export RAVENGUN_GLUT_LOG="$w/log/B.glut.log"
export RAVENGUN_BESTAND_LOG="$w/log/B.bestand.log"
export RAVENGUN_FORK_LOG="$w/log/B.fork.log"
export RAVENGUN_PTRACE_LOG="$w/log/B.ptrace.log"
export RAVENGUN_PTRACER_LOG="$w/log/B.permit.log"
exec timeout 45 env LD_PRELOAD="$W/libravengun-glut-log.so:$W/libravengun-ptracer-permit.so:$W/libravengun-bestand-log.so:$W/libravengun-fork-log.so:$W/libravengun-ptrace-log.so" "$LOADER" "$w/view/game" -n -f1360x768
EOF
t0=$(date +%s); "$TV" -- /bin/bash "$w/binnen.sh" >"$w/log/B.out" 2>&1 & bg=$!
for i in 1 2 3 4; do sleep 0.7; "$TV" -- spectacle -b -n -f -o "$w/shot-$i.png" >/dev/null 2>&1 || true; done
wait $bg 2>/dev/null; rc=$?; t1=$(date +%s); kill "$SOC" 2>/dev/null
printf '  exit=%s  levensduur=%ss  FOUT=%s\n' "$rc" "$((t1-t0))" "$(grep -c FOUT "$w/log/B.bestand.log" 2>/dev/null || echo 0)"
kop "HET ANTWOORD: het GLUT-logboek"
if [ -s "$w/log/B.glut.log" ]; then
  sed 's/^/  /' "$w/log/B.glut.log"
  echo
  printf '  glutCreateWindow aangeroepen : %s keer\n' "$(grep -c 'glutCreateWindow' "$w/log/B.glut.log")"
  printf '  glutMainLoop binnengekomen   : %s keer\n' "$(grep -c 'glutMainLoop BINNENGEKOMEN' "$w/log/B.glut.log")"
  printf '  glutSwapBuffers              : %s keer\n' "$(grep -c 'glutSwapBuffers' "$w/log/B.glut.log")"
  if grep -q 'glutCreateWindow' "$w/log/B.glut.log"; then
    echo "  ==> WEL aangeroepen: de retourwaarde hierboven is het antwoord (id 0 of -1 = mislukt)."
  else
    echo "  ==> NIET aangeroepen: de gast maakt geen venster aan -- een gast-tak, geen displayprobleem."
    echo "      (vergelijk: het kind dat onvoorwaardelijk exit(0) doet -- dezelfde vorm.)"
  fi
else
  echo "  GEEN glut-logboek: de wikkel is niet geladen, of de gast raakte GLUT nooit aan."
  echo "  -- wat de gast importeert en wat de lader laadde --"
  grep -nE 'libglut|glut' "$w/log/B.out" | head -5 | sed 's/^/    /'
fi
BEW="$D/logs-glut-$(date +%Y%m%d-%H%M)"; mkdir -p "$BEW"; cp "$w/log/"* "$BEW/" 2>/dev/null; cp "$w"/shot-*.png "$BEW/" 2>/dev/null
echo "  bewaard: $BEW"
rm -rf "$w/view" "$w/log" 2>/dev/null
for p in /g3 /T4User; do [ -e "$p" ] && echo "  LET OP: $p bestaat" || echo "  $p bestaat niet (goed)"; done
