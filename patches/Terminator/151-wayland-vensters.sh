#!/bin/bash
# ts/151-wayland-vensters.sh
#   1. BENCH-VRIJ: wat exposeert KWin via D-Bus? (zodat "geen venster" en "een Wayland-venster"
#      niet langer hetzelfde antwoord zijn)
#   2. Een run met dezelfde opzet, met de KWin-vraag ervoor en ernaast, en de frames met hash.
#   EN een deductie die ik eerst wil uitspreken: de run van 14:07 had het bord EN de saver UIT en toen
#   tekende het spel ook niet. Dus "het bord houdt de focus" is als ENIGE oorzaak al uitgesloten; wat er
#   ook veranderde, het is niet alleen de focus van het bord.
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
W=/tmp/ts-wayland
DO_START=0; [ "${1:-}" = "--start" ] && DO_START=1
rm -rf "$W"; mkdir -p "$W"
kop(){ printf '\n=== %s ===\n' "$*"; }

kop "1. WAT EXPOSEERT KWIN? (D-Bus, read-only)"
"$TV" -- qdbus6 org.kde.KWin 2>&1 | head -20 | sed 's/^/  /'
echo "  -- methoden op /KWin --"
"$TV" -- qdbus6 org.kde.KWin /KWin 2>&1 | head -30 | sed 's/^/  /'
echo "  -- en een paar directe vragen --"
for m in activeOutputName queryWindowInfo; do
  printf '    %s: %s\n' "$m" "$("$TV" -- timeout 3 qdbus6 org.kde.KWin /KWin org.kde.KWin.$m 2>&1 | head -2 | tr '\n' ' ')"
done
echo "  -- is er een scripting-interface? (voor een vensterlijst) --"
"$TV" -- qdbus6 org.kde.KWin /Scripting 2>&1 | head -12 | sed 's/^/    /'
echo "  -- en kdotool/wayland-hulpmiddelen? --"
for t in kdotool wayland-info wlr-randr; do p=$(command -v $t 2>/dev/null); [ -n "$p" ] && echo "    $t: $p"; done

for f in bestand-log.c ptracer-permit.c fork-log.c ptrace-log.c; do
  gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/libravengun-${f%.c}.so" "$D/$f" -ldl 2>&1 | head -3
done
"$TV" --check >/dev/null 2>&1 || { echo "  geen scherm"; exit 1; }
if [ "$DO_START" != 1 ]; then kop "KLAAR (bench-vrij deel gedaan)"; exit 0; fi

kop "2. DE RUN, MET DE KWIN-VRAAG ERNAAST"
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
t0=$(date +%s); "$TV" -- /bin/bash "$w/binnen.sh" >"$w/log/B.out" 2>&1 & bg=$!
sleep 1.2
echo "  -- tijdens de run: KWin-uitvoer --" | tee "$w/kwin.txt"
"$TV" -- qdbus6 org.kde.KWin /KWin org.kde.KWin.activeOutputName 2>&1 | sed 's/^/    /' | tee -a "$w/kwin.txt"
echo "  -- en de X11-lijst --" | tee -a "$w/kwin.txt"
"$TV" -- xdotool search --name . getwindowname %@ 2>/dev/null | sort -u | sed 's/^/    /' | tee -a "$w/kwin.txt"
for i in 1 2 3 4 5; do sleep 0.6; "$TV" -- spectacle -b -n -f -o "$w/shot-$i.png" >/dev/null 2>&1 || true; done
wait $bg 2>/dev/null; rc=$?; t1=$(date +%s); kill "$SOC" 2>/dev/null
printf '  exit=%s  levensduur=%ss  FOUT=%s\n' "$rc" "$((t1-t0))" "$(grep -c FOUT "$w/log/B.bestand.log" 2>/dev/null || echo 0)"
echo "  -- opnamen --"
for s in "$w"/shot-*.png; do [ -f "$s" ] && printf '    %-12s %9s B  %s\n' "$(basename "$s")" "$(stat -c%s "$s")" "$(sha256sum "$s" | cut -c1-12)"; done
BEW="$D/logs-wayland-$(date +%Y%m%d-%H%M)"; mkdir -p "$BEW"; cp "$w/log/"* "$BEW/" 2>/dev/null; cp "$w"/shot-*.png "$BEW/" 2>/dev/null; cp "$w/kwin.txt" "$BEW/" 2>/dev/null
echo "  bewaard: $BEW"
rm -rf "$w/view" "$w/log" 2>/dev/null
for p in /g3 /T4User; do [ -e "$p" ] && echo "  LET OP: $p bestaat" || echo "  $p bestaat niet (goed)"; done
