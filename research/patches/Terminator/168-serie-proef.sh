#!/bin/bash
# ts/168-serie-proef.sh - DE SERIËLE PROEF. Wat zegt de gast tegen de poort, en waar wacht hij op?
#   Opzet: een socat-PTY die IN DE SANDBOX als /dev/ttyS0 wordt gebonden (host ongemoeid), plus de
#   serie-wikkel met laadbewijs. De verwachtingen staan in de kop van serie-log.c.
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
W=/tmp/ts-serie
DO_START=0; [ "${1:-}" = "--start" ] && DO_START=1
rm -rf "$W"; mkdir -p "$W"
kop(){ printf '\n=== %s ===\n' "$*"; }

kop "VOORBEREIDING (bench-vrij)"
# LET OP: NIET `gcc ... | head -N` -- dat doodt de compiler met SIGPIPE zodra hij meer dan N regels
# diagnostiek geeft (waarschuwingen tellen mee!), en dan is de .so niet geschreven terwijl je alleen
# een waarschuwing ziet. Dat is mij hier overkomen. Uitvoer naar een bestand, en dan kijken.
for f in serie-log.c bestand-log.c ptracer-permit.c fork-log.c ptrace-log.c; do
  out="libravengun-${f%.c}.so"
  gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/$out" "$D/$f" -ldl >"$W/gcc-${f%.c}.log" 2>&1
  if [ -s "$W/$out" ]; then echo "  OK    $out"; else echo "  FOUT  $out -- compiler zei:"; sed 's/^/        /' "$W/gcc-${f%.c}.log" | head -12; fi
done
[ -s "$W/libravengun-serie-log.so" ] || { echo "  STOP: serie-wikkel niet gebouwd"; exit 1; }
echo "  -- wat de wikkel exporteert --"
nm -D --defined-only "$W/libravengun-serie-log.so" 2>/dev/null | awk '{print "    " $3}' | sort
echo
echo "  -- en de vooraf vastgelegde verwachting (uit de kop van serie-log.c) --"
sed -n '/VOORAF VASTGELEGD/,/Logboek:/p' "$D/serie-log.c" | sed 's/^/    /'
"$TV" --check >/dev/null 2>&1 && echo "  OK: scherm bereikbaar" || { echo "  geen scherm"; exit 1; }
if [ "$DO_START" != 1 ]; then kop "KLAAR"; exit 0; fi

kop "DE RUN: PTY als /dev/ttyS0 IN de sandbox (host ongemoeid)"
w="$W/run"; mkdir -p "$w/log" "$w/upper" "$w/work" "$w/view"
cp "$GAME" "$w/view/game"
for d in T4Data ServiceDongleApp hasp env irgun GeneralPurposeStateMachine.txt; do
  [ -e "$NASDIR/$d" ] && ln -sfn "$NASDIR/$d" "$w/view/$d"
done
[ -e "$NASDIR/T4User" ] && cp -r "$NASDIR/T4User" "$w/view/T4User"
cp "$NASDIR/T4User/log/syslog_bb3.txt" "$w/view/syslog_bb3.txt" 2>/dev/null
socat -d -d pty,raw,echo=0,link="$w/ttyS0" pty,raw,echo=0,link="$w/ttyS0-peer" >"$w/socat.log" 2>&1 &
SOC=$!; for i in $(seq 1 20); do [ -L "$w/ttyS0" ] && break; sleep 0.2; done
socat -d -d pty,raw,echo=0,link="$w/sierra2" pty,raw,echo=0,link="$w/sierra2-peer" >>"$w/socat.log" 2>&1 &
SOC2=$!; for i in $(seq 1 20); do [ -L "$w/sierra2" ] && break; sleep 0.2; done
printf '  PTY ttyS0  -> %s\n  PTY sierra2 -> %s\n' "$(readlink -f "$w/ttyS0" 2>/dev/null)" "$(readlink -f "$w/sierra2" 2>/dev/null)"

BW=(bwrap --die-with-parent
    --ro-bind /usr /usr
    --symlink usr/lib /lib --symlink usr/lib64 /lib64 --symlink usr/bin /bin --symlink usr/bin /sbin
    --ro-bind /etc /etc --proc /proc --ro-bind /sys /sys
    --ro-bind /home /home --ro-bind /var /var --ro-bind /opt /opt --ro-bind /boot /boot --ro-bind /srv /srv)
[ -e /root ] && BW+=(--ro-bind /root /root)
BW+=(--dev /dev --dev-bind-try /dev/pts /dev/pts --dev-bind-try /dev/bus/usb /dev/bus/usb)
for dn in null zero full random urandom tty shm dri snd input; do
  [ -e "/dev/$dn" ] && BW+=(--dev-bind-try "/dev/$dn" "/dev/$dn")
done
# DE KNOPEN: onze PTY's OVER de echte apparaten heen, alleen in de sandbox
[ -L "$w/ttyS0" ]   && BW+=(--dev-bind "$(readlink -f "$w/ttyS0")"   /dev/ttyS0)
[ -L "$w/sierra2" ] && BW+=(--dev-bind "$(readlink -f "$w/sierra2")" /dev/sierra2)
BW+=(--ro-bind /run /run --bind-try /run/user/1000 /run/user/1000 --bind /tmp /tmp)
BW+=(--dir /g3 --ro-bind "$NASDIR" /g3)
BW+=(--overlay-src "$NASDIR/T4User" --overlay "$w/upper" "$w/work" /T4User)

echo "  -- controle BINNEN de sandbox: zijn de knopen er en zijn ze te openen? --"
"${BW[@]}" /bin/sh -c 'ls -l /dev/ttyS0 /dev/sierra2 2>&1; exec 3<>/dev/ttyS0 2>/dev/null && echo "ttyS0: OPEN" || echo "ttyS0: NIET"' 2>&1 | sed 's/^/    /'

cat >"$w/binnen.sh" <<EOF
cd "$w/view" || exit 1
export LD_LIBRARY_PATH="$EL/libs:$LB32"
export RG_ELFLOADER_VERBOSE=1
export RAVENGUN_SERIE_LOG="$w/log/B.serie.log"
export RAVENGUN_BESTAND_LOG="$w/log/B.bestand.log"
export RAVENGUN_FORK_LOG="$w/log/B.fork.log"
export RAVENGUN_PTRACE_LOG="$w/log/B.ptrace.log"
export RAVENGUN_PTRACER_LOG="$w/log/B.permit.log"
exec timeout 45 env LD_PRELOAD="$W/libravengun-serie-log.so:$W/libravengun-ptracer-permit.so:$W/libravengun-bestand-log.so:$W/libravengun-fork-log.so:$W/libravengun-ptrace-log.so" "$LOADER" "$w/view/game" -n -f1360x768
EOF
t0=$(date +%s); "$TV" -- /bin/bash "$w/binnen.sh" >"$w/log/B.out" 2>&1 &
bg=$!; for i in 1 2 3 4; do sleep 0.7; "$TV" -- spectacle -b -n -f -o "$w/shot-$i.png" >/dev/null 2>&1 || true; done
wait $bg 2>/dev/null; rc=$?; t1=$(date +%s); kill "$SOC" "$SOC2" 2>/dev/null
printf '  exit=%s  levensduur=%ss  FOUT=%s\n' "$rc" "$((t1-t0))" "$(grep -c FOUT "$w/log/B.bestand.log" 2>/dev/null || echo 0)"

kop "HET SERIËLE LOGBOEK — wat de gast zegt en waar hij op wacht"
if [ -s "$w/log/B.serie.log" ]; then
  sed 's/^/  /' "$w/log/B.serie.log"
  echo
  printf '  open-pogingen op een poort : %s\n' "$(grep -c 'SERIE open' "$w/log/B.serie.log")"
  printf '  geslaagde opens            : %s\n' "$(grep -c 'SERIE open.*fd=' "$w/log/B.serie.log")"
  printf '  ioctl-aanroepen            : %s\n' "$(grep -c 'SERIE ioctl' "$w/log/B.serie.log")"
  printf '  SCHRIJFACTIES (bytes eruit): %s\n' "$(grep -c 'SERIE write' "$w/log/B.serie.log")"
  printf '  LEESACTIES                 : %s\n' "$(grep -c 'SERIE read' "$w/log/B.serie.log")"
  if grep -q 'SERIE write' "$w/log/B.serie.log"; then
    echo "  ==> ER WORDT GESCHREVEN: de gast stuurt een sonde. Volgende stap: een ANTWOORD namaken."
    grep 'SERIE write' "$w/log/B.serie.log" | sed 's/^/      /'
  elif grep -q 'SERIE ioctl' "$w/log/B.serie.log"; then
    echo "  ==> Alleen geconfigureerd (ioctl), NIETS geschreven: hij zoekt alleen aanwezigheid."
  else
    echo "  ==> Alleen geopend, niets geconfigureerd of geschreven."
  fi
else
  echo "  GEEN serie-logboek. -- is de wikkel geladen? --"
  grep -c 'WIKKEL GELADEN' "$w/log/B.out" 2>/dev/null | sed 's/^/    WIKKEL-regels in B.out: /'
  echo "    (geen laadregel = de wikkel is niet geladen; dan is dit GEEN uitspraak over de gast)"
fi
BEW="$D/logs-serie-$(date +%Y%m%d-%H%M)"; mkdir -p "$BEW"; cp "$w/log/"* "$BEW/" 2>/dev/null; cp "$w"/shot-*.png "$BEW/" 2>/dev/null
echo "  bewaard: $BEW"
rm -rf "$w/view" "$w/log" 2>/dev/null
for p in /g3 /T4User; do [ -e "$p" ] && echo "  LET OP: $p bestaat" || echo "  $p bestaat niet (goed)"; done
