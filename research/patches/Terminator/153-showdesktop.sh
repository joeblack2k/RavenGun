#!/bin/bash
# ts/153-showdesktop-en-vensters.sh
#   DE GOEDKOPE TOETS DIE ALLES KAN VERKLAREN: in KWin verbergt `showingDesktop = true` ALLE vensters en
#   laat alleen het bureaublad zien -- precies wat ik om 14:07 fotografeerde (bureaublad, geen spel).
#   Dus: lees die eigenschap, draai de run met showDesktop(false), en ZET HEM DAARNA TERUG in dezelfde run.
#
#   BELEEFDHEID (op verzoek): showDesktop(bool) raakt het bureaublad van de eigenaar. Ik lees de waarde
#   VOORAF, verberg alleen tijdens de run, en zet hem aan het eind terug naar die waarde. Beide aanroepen
#   staan in het verslag.
#
#   EN: het KWin-script voor de Wayland-vensterlijst, geladen via /Scripting en aan het eind ONTLADEN,
#   zodat er niets in KWin's configuratie achterblijft.
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
W=/tmp/ts-sd
DO_START=0; [ "${1:-}" = "--start" ] && DO_START=1
rm -rf "$W"; mkdir -p "$W"
kop(){ printf '\n=== %s ===\n' "$*"; }
sd_lees(){ "$TV" -- timeout 3 qdbus6 org.kde.KWin /KWin org.freedesktop.DBus.Properties.Get org.kde.KWin showingDesktop 2>/dev/null | tail -1; }
sd_zet(){ "$TV" -- timeout 3 qdbus6 org.kde.KWin /KWin org.kde.KWin.showDesktop "$1" >/dev/null 2>&1; }

# het KWin-script: print() gaat naar het journal, met een unieke merkstring
cat >"$W/vensters.js" <<'JS'
function dump(waarom) {
    var ws = workspace.windowList ? workspace.windowList() : workspace.clientList();
    print("TSVENSTER begin " + waarom + " aantal=" + ws.length);
    for (var i = 0; i < ws.length; i++) {
        var w = ws[i];
        var g = w.geometry ? (w.geometry.x + "," + w.geometry.y + " " + w.geometry.width + "x" + w.geometry.height) : "?";
        print("TSVENSTER " + (w.resourceClass || w.resourceName || "?") + " | " + (w.caption || "") + " | " + g + " | actief=" + (workspace.activeWindow === w));
    }
    print("TSVENSTER eind " + waarom);
}
dump("bij-laden");
workspace.windowAdded.connect(function (w) { print("TSVENSTER toegevoegd " + (w.resourceClass || "?") + " | " + (w.caption || "")); });
workspace.windowActivated.connect(function (w) { print("TSVENSTER geactiveerd " + (w && w.resourceClass ? w.resourceClass : "?") + " | " + (w && w.caption ? w.caption : "")); });
JS

kop "1. DE STAAT VOORAF"
SD_VOOR=$(sd_lees); printf '  showingDesktop VOOR = %s\n' "${SD_VOOR:-?}"
echo "  (true = KWin verbergt ALLE vensters en toont alleen het bureaublad)"
echo "  -- X11-lijst vooraf --"; "$TV" -- xdotool search --name . getwindowname %@ 2>/dev/null | sort -u | head -12 | sed 's/^/    /'
echo "  -- het script laden --"
ID=$("$TV" -- timeout 5 qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$W/vensters.js" tsvenster 2>&1 | tail -1)
printf '  loadScript gaf id %s\n' "${ID:-?}"
"$TV" -- timeout 5 qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.start >/dev/null 2>&1
sleep 1
echo "  -- wat het script bij het laden zag (uit het journal) --"
journalctl --user --since '-1 min' --no-pager 2>/dev/null | grep 'TSVENSTER' | tail -15 | sed 's/^/    /'

for f in bestand-log.c ptracer-permit.c fork-log.c ptrace-log.c; do
  gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/libravengun-${f%.c}.so" "$D/$f" -ldl 2>&1 | head -3
done
if [ "$DO_START" != 1 ]; then
  kop "KLAAR"; echo "  Draai met: bash $0 --start"
  "$TV" -- timeout 3 qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript tsvenster >/dev/null 2>&1
  exit 0
fi

kop "2. DE RUN MET showDesktop(false) EN HET VENSTERSCRIPT"
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
# DE AANROEP DIE HET BUREAUBLAD TIJDELIJK TERUGNEEMT: showDesktop(false) = vensters weer zichtbaar
echo "  AANROEP: showDesktop(false)  (vensters weer zichtbaar tijdens de run)"
sd_zet false
t0=$(date +%s); "$TV" -- /bin/bash "$w/binnen.sh" >"$w/log/B.out" 2>&1 & bg=$!
for i in 1 2 3 4 5 6; do sleep 0.55; "$TV" -- spectacle -b -n -f -o "$w/shot-$i.png" >/dev/null 2>&1 || true; done
wait $bg 2>/dev/null; rc=$?; t1=$(date +%s); kill "$SOC" 2>/dev/null
# HERSTEL, in dezelfde run
SD_NA=$(sd_lees)
echo "  AANROEP: herstel naar de staat van vooraf (showingDesktop was ${SD_VOOR:-?}, nu ${SD_NA:-?})"
case "${SD_VOOR:-}" in *true*) sd_zet true ;; *) sd_zet false ;; esac
printf '  showingDesktop NA HERSTEL = %s\n' "$(sd_lees)"
# en het script eruit, zodat er niets achterblijft
"$TV" -- timeout 3 qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript tsvenster >/dev/null 2>&1
printf '  script nog geladen? %s\n' "$("$TV" -- timeout 3 qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.isScriptLoaded tsvenster 2>&1 | tail -1)"
printf '  exit=%s  levensduur=%ss  FOUT=%s\n' "$rc" "$((t1-t0))" "$(grep -c FOUT "$w/log/B.bestand.log" 2>/dev/null || echo 0)"
echo "  -- opnamen --"
for s in "$w"/shot-*.png; do [ -f "$s" ] && printf '    %-12s %9s B  %s\n' "$(basename "$s")" "$(stat -c%s "$s")" "$(sha256sum "$s" | cut -c1-12)"; done
echo "  -- WAT HET SCRIPT TIJDENS DE RUN ZAG (Wayland-vensters!) --"
journalctl --user --since '-2 min' --no-pager 2>/dev/null | grep 'TSVENSTER' | tail -25 | sed 's/^/    /'
BEW="$D/logs-showdesktop-$(date +%Y%m%d-%H%M)"; mkdir -p "$BEW"
cp "$w/log/"* "$BEW/" 2>/dev/null; cp "$w"/shot-*.png "$BEW/" 2>/dev/null; cp "$W/vensters.js" "$BEW/" 2>/dev/null
journalctl --user --since '-3 min' --no-pager 2>/dev/null | grep 'TSVENSTER' > "$BEW/wayland-vensters.log" 2>/dev/null
{ echo "run: showDesktop(false) + KWin-vensterscript"; echo "datum: $(date -Is)"
  echo "showingDesktop VOOR : ${SD_VOOR:-?}"; echo "showingDesktop NA    : $(sd_lees)"
  echo "AANROEP 1: org.kde.KWin.showDesktop(false)   (tijdens de run)"
  echo "AANROEP 2: herstel naar de staat van vooraf"
  echo "AANROEP 3: org.kde.kwin.Scripting.unloadScript tsvenster   (script eruit)"
  echo "exitcode: $rc"; echo "levensduur: $((t1-t0)) s"; echo "FOUT: $(grep -c FOUT "$w/log/B.bestand.log")"
  echo "-- opnamen --"; for s in "$BEW"/shot-*.png; do [ -f "$s" ] && echo "$(basename "$s") $(stat -c%s "$s") $(sha256sum "$s" | cut -c1-12)"; done; } > "$BEW/RESULT.txt"
echo "  bewaard: $BEW"
rm -rf "$w/view" "$w/log" 2>/dev/null
for p in /g3 /T4User; do [ -e "$p" ] && echo "  LET OP: $p bestaat" || echo "  $p bestaat niet (goed)"; done
