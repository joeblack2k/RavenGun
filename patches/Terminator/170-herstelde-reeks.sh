#!/bin/bash
# ts/170-herstelde-reeks.sh - DE HERSTELDE RUN: in de sandbox, met de drie controles erin.
#
# WAT ER MIS WAS (en wat dit script herstelt): de run-regel in mijn displayscripts had geen
# `"${BW[@]}"`, dus de runs gingen de sandbox NIET in. Gevolg: geen /g3, geen /T4User (FOUT 21 in
# plaats van 8), geen PTY-knopen, en het echte hostapparaat /dev/ttyS0 met EPERM. Daarmee is
# "de koppelingen falen onder de display-route" ONJUIST en teruggetrokken: ze zijn nooit getest.
#
# DE DRIE CONTROLES, alle drie IN de run zelf:
#   1. `"${BW[@]}"` op de run-regel (en dat is de eerste regel die ik narek);
#   2. de knoopcontrole in dezelfde aanroep die de gast start;
#   3. BEWIJS DAT DE RUN IN EEN SANDBOX ZAT: de mount-namespace en het bestaan van /g3 en /T4User,
#      geschreven in het runlogboek. Zo is het een feit uit het artefact en geen aanname.
#
# VOORAF VASTGELEGD:
#   * FOUT moet nu **8** zijn (niet 21) -> dan werken de koppelingen onder de display-route wél.
#   * De GLUT-vraag wordt OPNIEUW gemeten, nu binnen de sandbox. Verwachting ongewijzigd: nul
#     glut-aanroepen = de gast neemt een vensterloze tak.
#   * De serie-vraag: met een werkende knoop op /dev/ttyS0 -> ofwel een SONDE met wachtend `read`
#     (dan is een synthetisch antwoord begrensd werk), ofwel alleen `ioctl` zonder bytes.
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
W=/tmp/ts-hersteld
DO_START=0; [ "${1:-}" = "--start" ] && DO_START=1
rm -rf "$W"; mkdir -p "$W"
kop(){ printf '\n=== %s ===\n' "$*"; }

kop "1. BOUWEN (uitvoer naar een BESTAND -- nooit door head, dat doodt de compiler)"
for f in serie-log.c glut-log.c bestand-log.c ptracer-permit.c fork-log.c ptrace-log.c; do
  out="libravengun-${f%.c}.so"
  gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/$out" "$D/$f" -ldl >"$W/gcc-${f%.c}.log" 2>&1
  if [ -s "$W/$out" ]; then echo "  OK    $out"; else echo "  FOUT  $out:"; head -8 "$W/gcc-${f%.c}.log" | sed 's/^/        /'; fi
done
[ -s "$W/libravengun-glut-log.so" ] && [ -s "$W/libravengun-serie-log.so" ] || { echo "  STOP"; exit 1; }

"$TV" --check >/dev/null 2>&1 || { echo "  geen scherm"; exit 1; }
bezet(){ local p; for p in gamescope gamescope-wl rg-elfloader wine wine64 BudgieLoader VACUUM.exe game; do
  pgrep -x "$p" >/dev/null 2>&1 && { printf '%s ' "$p"; return 0; }; done; return 1; }
if b=$(bezet); then echo "  BEZET: $b"; [ "$DO_START" = 1 ] && exit 1; fi
[ "$DO_START" = 1 ] || { kop "KLAAR"; exit 0; }

kop "2. KNOPEN MAKEN (buiten de sandbox; alleen in de sandbox gebonden)"
w="$W/run"; mkdir -p "$w/log" "$w/upper" "$w/work" "$w/view"
cp "$GAME" "$w/view/game"
for d in T4Data ServiceDongleApp hasp env irgun GeneralPurposeStateMachine.txt; do
  [ -e "$NASDIR/$d" ] && ln -sfn "$NASDIR/$d" "$w/view/$d"
done
[ -e "$NASDIR/T4User" ] && cp -r "$NASDIR/T4User" "$w/view/T4User"
cp "$NASDIR/T4User/log/syslog_bb3.txt" "$w/view/syslog_bb3.txt" 2>/dev/null
socat -d -d pty,raw,echo=0,link="$w/ttyS0" pty,raw,echo=0,link="$w/ttyS0p" >"$w/socat.log" 2>&1 &
SOC=$!; for i in $(seq 1 30); do [ -L "$w/ttyS0" ] && break; sleep 0.2; done
socat -d -d pty,raw,echo=0,link="$w/sierra2" pty,raw,echo=0,link="$w/sierra2p" >>"$w/socat.log" 2>&1 &
SOC2=$!; for i in $(seq 1 30); do [ -L "$w/sierra2" ] && break; sleep 0.2; done
printf '  ttyS0   -> %s\n  sierra2 -> %s\n' "$(readlink -f "$w/ttyS0" 2>/dev/null)" "$(readlink -f "$w/sierra2" 2>/dev/null)"

# DE VOLGORDE: eerst --dev (de eigen devtmpfs), DAN de knopen erover, DAN de rest.
BW=(bwrap --die-with-parent
    --ro-bind /usr /usr
    --symlink usr/lib /lib --symlink usr/lib64 /lib64 --symlink usr/bin /bin --symlink usr/bin /sbin
    --ro-bind /etc /etc --proc /proc --ro-bind /sys /sys
    --ro-bind /home /home --ro-bind /var /var --ro-bind /opt /opt --ro-bind /boot /boot --ro-bind /srv /srv)
[ -e /root ] && BW+=(--ro-bind /root /root)
BW+=(--dev /dev)
BW+=(--dev-bind-try /dev/pts /dev/pts)
for dn in null zero full random urandom tty shm dri snd input; do
  [ -e "/dev/$dn" ] && BW+=(--dev-bind-try "/dev/$dn" "/dev/$dn")
done
# ONZE KNOPEN, NA --dev, zodat niets ze overschrijft
[ -L "$w/ttyS0" ]   && BW+=(--dev-bind "$(readlink -f "$w/ttyS0")"   /dev/ttyS0)
[ -L "$w/sierra2" ] && BW+=(--dev-bind "$(readlink -f "$w/sierra2")" /dev/sierra2)
BW+=(--ro-bind /run /run --bind-try /run/user/1000 /run/user/1000 --bind /tmp /tmp)
BW+=(--dir /g3 --ro-bind "$NASDIR" /g3)
BW+=(--overlay-src "$NASDIR/T4User" --overlay "$w/upper" "$w/work" /T4User)

cat >"$w/binnen.sh" <<EOF
# CONTROLE 3: bewijs dat deze run IN de sandbox zit -- in het runlogboek, uit de run zelf
{
  echo "SANDBOX ns-mnt : \$(readlink /proc/self/ns/mnt)"
  echo "SANDBOX /g3    : \$( [ -d /g3 ] && echo ja || echo NEE )  (/g3/T4Data: \$( [ -d /g3/T4Data ] && echo ja || echo nee ))"
  echo "SANDBOX /T4User: \$( [ -d /T4User ] && echo ja || echo NEE )"
  echo "SANDBOX knoop  : \$(ls -l /dev/ttyS0 2>&1 | tr -s ' ')"
  echo "SANDBOX sierra : \$(ls -l /dev/sierra2 2>&1 | tr -s ' ')"
} > "$w/log/controles.txt" 2>&1
# CONTROLE 2: de knoop echt OPENEN, in dezelfde aanroep als de gast
{ exec 7<>/dev/ttyS0 && echo "knoop ttyS0 : OPEN" || echo "knoop ttyS0 : NIET OPEN"; } >>"$w/log/controles.txt" 2>&1
{ exec 8<>/dev/sierra2 && echo "knoop sierra: OPEN" || echo "knoop sierra: NIET OPEN"; } >>"$w/log/controles.txt" 2>&1
cd "$w/view" || exit 1
export LD_LIBRARY_PATH="$EL/libs:$LB32"
export RG_ELFLOADER_VERBOSE=1
export RAVENGUN_GLUT_LOG="$w/log/B.glut.log"
export RAVENGUN_SERIE_LOG="$w/log/B.serie.log"
export RAVENGUN_BESTAND_LOG="$w/log/B.bestand.log"
export RAVENGUN_FORK_LOG="$w/log/B.fork.log"
export RAVENGUN_PTRACE_LOG="$w/log/B.ptrace.log"
export RAVENGUN_PTRACER_LOG="$w/log/B.permit.log"
exec timeout 45 env LD_PRELOAD="$W/libravengun-glut-log.so:$W/libravengun-serie-log.so:$W/libravengun-ptracer-permit.so:$W/libravengun-bestand-log.so:$W/libravengun-fork-log.so:$W/libravengun-ptrace-log.so" "$LOADER" "$w/view/game" -n -f1360x768
EOF

kop "3. DE RUN -- in de SANDBOX, met de displayomgeving"
t0=$(date +%s)
# LET OP: hier staat \"\${BW[@]}\" -- dat was de fout in alle vorige displayscripts
"$TV" -- "${BW[@]}" /bin/bash "$w/binnen.sh" >"$w/log/B.out" 2>&1 &
bg=$!; for i in 1 2 3 4; do sleep 0.7; "$TV" -- spectacle -b -n -f -o "$w/shot-$i.png" >/dev/null 2>&1 || true; done
wait $bg 2>/dev/null; rc=$?; t1=$(date +%s); kill "$SOC" "$SOC2" 2>/dev/null
echo "  -- DE DRIE CONTROLES (uit de run zelf) --"; sed 's/^/    /' "$w/log/controles.txt" 2>/dev/null
printf '  exit=%s  levensduur=%ss  FOUT=%s\n' "$rc" "$((t1-t0))" "$(grep -c FOUT "$w/log/B.bestand.log" 2>/dev/null || echo 0)"
echo "  ==> FOUT 8 = de koppelingen WERKEN in de sandbox (21 zou betekenen: weer geen sandbox)"
f=$(grep -c FOUT "$w/log/B.bestand.log" 2>/dev/null || echo 99)
[ "$f" = "8" ] && echo "      JA: 8 -> de koppelingen zijn nu ECHT getest en ze werken." || echo "      LET OP: $f"

kop "4. DE GLUT-VRAAG, OPNIEUW, NU IN DE SANDBOX"
if [ -s "$w/log/B.glut.log" ]; then
  grep -c 'WIKKEL GELADEN' "$w/log/B.glut.log" | sed 's/^/  laadbewijs: /'
  printf '  glutInit=%s  glutCreateWindow=%s  glutEnterGameMode=%s  glutDisplayFunc=%s  glutMainLoop=%s  glutSwapBuffers=%s\n' \
    "$(grep -c glutInit "$w/log/B.glut.log")" "$(grep -c glutCreateWindow "$w/log/B.glut.log")" \
    "$(grep -c glutEnterGameMode "$w/log/B.glut.log")" "$(grep -c glutDisplayFunc "$w/log/B.glut.log")" \
    "$(grep -c glutMainLoop "$w/log/B.glut.log")" "$(grep -c glutSwapBuffers "$w/log/B.glut.log")"
  sed 's/^/    /' "$w/log/B.glut.log" | head -12
else echo "  geen glut-logboek"; fi

kop "5. DE SERIE-VRAAG: wat zegt de gast tegen de poort?"
if [ -s "$w/log/B.serie.log" ]; then
  sed 's/^/  /' "$w/log/B.serie.log"
  printf '  opens=%s  geslaagd=%s  ioctl=%s  SCHRIJF=%s  LEES=%s\n' \
    "$(grep -c 'SERIE open' "$w/log/B.serie.log")" "$(grep -c 'SERIE open.*fd=' "$w/log/B.serie.log")" \
    "$(grep -c 'SERIE ioctl' "$w/log/B.serie.log")" "$(grep -c 'SERIE write' "$w/log/B.serie.log")" \
    "$(grep -c 'SERIE read' "$w/log/B.serie.log")"
  if grep -q 'SERIE write' "$w/log/B.serie.log"; then echo "  ==> SONDE GESCHREVEN: een synthetisch antwoord is nu begrensd werk."
  elif grep -q 'SERIE ioctl' "$w/log/B.serie.log"; then echo "  ==> alleen configuratie (ioctl), geen bytes."
  else echo "  ==> alleen geopend."; fi
else echo "  geen serie-logboek"; fi

BEW="$D/logs-hersteld-$(date +%Y%m%d-%H%M)"; mkdir -p "$BEW"; cp "$w/log/"* "$BEW/" 2>/dev/null; cp "$w"/shot-*.png "$BEW/" 2>/dev/null
echo "  bewaard: $BEW"
rm -rf "$w/view" "$w/log" 2>/dev/null
for p in /g3 /T4User; do [ -e "$p" ] && echo "  LET OP: $p bestaat" || echo "  $p bestaat niet (goed)"; done
