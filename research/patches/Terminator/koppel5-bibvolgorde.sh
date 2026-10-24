#!/bin/bash
# koppel5-bibvolgorde.sh - ARM 2: de BIBLIOTHEekVOLGORDE. Een variabele t.o.v. meting 3:
# `lib32` staat nu VOOR `ElfLdr2/libs` in LD_LIBRARY_PATH, zodat libGLU.so.1 en libtheora.so.0 -- die in
# BEIDE mappen staan -- uit lib32 komen in plaats van uit ElfLdr2. Dat is de configuratie die het
# spelprofiel van het project zelf declareert (libs: lib32 voor libtheora/libGLU).
#
#   bash koppel5-bibvolgorde.sh            # voorbereiding + proef (bench-vrij). START NIETS.
#   bash koppel5-bibvolgorde.sh --start    # de run (max 90 s). START DE GAME.
#
# VOORAF VASTGELEGD:
#   * DE VARIABELE MOET ZICHTBAAR AANKOMEN: ik lees uit het laderlogboek van WELK pad libGLU.so.1 en
#     libtheora.so.0 geladen zijn. Staat daar runtime/lib32, dan is de variabele aangekomen -- net zo
#     hard als de argc 3 van arm 1. Zonder die controle is een nul niets waard.
#   * VERWACHTING: blijven de regels (89), de fouten (8) en de lus (0,011 s / 0,4 ms / 2 tegelijk)
#     identiek, dan drijven de BIBLIOTHEKEN de metronoom niet, en blijft de opzet van onze route
#     (sandbox, view, omgeving, of de lader zelf) de enige kandidaat aan onze kant.
#   * MOGELIJKE VERVUILING die ik vooraf benoem: lib32-libGLU kan andere afhankelijkheden hebben dan de
#     ElfLdr2-kopie. Als er daardoor een bibliotheek NIET meer laadt, dan test deze run niet "dezelfde
#     libs uit een andere herkomst" maar "een andere bibliotheekset" -- dan is de vergelijking minder
#     schoon en zeg ik dat. Ik controleer daarom of alle 13 nog geladen worden.
set -u
R="$HOME/projects/ravengun"
LOADER="$R/tools/rg-elfloader/rg-elfloader"
NASDIR="/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation "
GAME="$NASDIR/game"
EL="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2"
LB32="$HOME/.local/share/ravengun/runtime/lib32"
D="$HOME/projects/ravengun-patches/patches/Terminator"
INC="$R/tools/rg-elfloader/inc"
W=/tmp/ts-bib
DO_START=0; [ "${1:-}" = "--start" ] && DO_START=1
BASIS="$D/logs-koppel3-20261013-1335"
rm -rf "$W"; mkdir -p "$W/log" "$W/upper" "$W/work" "$W/view"
ok(){ printf '  OK    %s\n' "$*"; }; warn(){ printf '  LET OP %s\n' "$*"; }; kop(){ printf '\n=== %s ===\n' "$*"; }
SOCAT=""
bezet(){ local p; for p in gamescope gamescope-wl rg-elfloader wine wine64 BudgieLoader VACUUM.exe game; do
  pgrep -x "$p" >/dev/null 2>&1 && { printf '%s ' "$p"; return 0; }; done; return 1; }

kop "VOORBEREIDING"
if b=$(bezet); then warn "BEZET door: $b -- ik start NIET"; [ "$DO_START" = 1 ] && exit 1; else ok "bench vrij"; fi
printf '  lader: %s\n' "$(sha256sum "$LOADER" | cut -d' ' -f1)"
echo "  -- de twee kandidaten voor libGLU en libtheora --"
for l in libGLU.so.1 libtheora.so.0; do
  printf '    %-18s ElfLdr2 %s B (%s)   lib32 %s B (%s)\n' "$l" \
    "$(stat -c%s "$EL/libs/$l" 2>/dev/null || echo -)" "$(sha256sum "$EL/libs/$l" 2>/dev/null | cut -c1-12)" \
    "$(stat -c%s "$LB32/$l" 2>/dev/null || echo -)" "$(sha256sum "$LB32/$l" 2>/dev/null | cut -c1-12)"
done
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

kop "DE RUN MET lib32 VOOR ElfLdr2"
cat >"$W/binnen.sh" <<EOF
cd "$W/view" || exit 1
export LD_LIBRARY_PATH="$LB32:$EL/libs"
export RG_ELFLOADER_VERBOSE=1
export RAVENGUN_BESTAND_LOG="$W/log/B.bestand.log"
export RAVENGUN_FORK_LOG="$W/log/B.fork.log"
export RAVENGUN_PTRACE_LOG="$W/log/B.ptrace.log"
export RAVENGUN_PTRACER_LOG="$W/log/B.permit.log"
exec timeout 90 env LD_PRELOAD="$W/libravengun-ptracer-permit.so:$W/libravengun-bestand-log.so:$W/libravengun-fork-log.so:$W/libravengun-ptrace-log.so" "$LOADER" "$W/view/game"
EOF
t0=$(date +%s); "${BW[@]}" /bin/bash "$W/binnen.sh" >"$W/log/B.out" 2>&1; rc=$?; t1=$(date +%s)
[ -n "$SOCAT" ] && kill "$SOCAT" 2>/dev/null
BEW="$D/logs-bibvolgorde-$(date +%Y%m%d-%H%M)"; mkdir -p "$BEW"; cp "$W/log/"* "$BEW/" 2>/dev/null
{ echo "arm            : bibliotheekvolgorde (lib32 VOOR ElfLdr2/libs voor libGLU + libtheora)"
  echo "datum          : $(date -Is)"; echo "exitcode       : $rc"; echo "levensduur     : $((t1-t0)) s"
  echo "LD_LIBRARY_PATH: $LB32:$EL/libs"
  echo "lader sha256   : $(sha256sum "$LOADER" | cut -d' ' -f1)"; echo "gast sha256    : $(sha256sum "$GAME" | cut -d' ' -f1)"
  echo "kern sha256    : $(sha256sum "$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/TeknoParrot/TeknoParrot64.dll" | cut -d' ' -f1)"
  echo "grens          : NAS onaangeroerd (overlay-upper in /tmp); alleen eigen PIDs opgeruimd"
  echo "regels         : $(wc -l < "$W/log/B.bestand.log" 2>/dev/null || echo 0)"
  echo "FOUT           : $(grep -c FOUT "$W/log/B.bestand.log" 2>/dev/null || echo 0)"
  echo "ATTACH ok      : $(grep -c 'PTRACE_ATTACH.*ok' "$W/log/B.ptrace.log" 2>/dev/null || echo 0)"
} > "$BEW/RESULT.txt"
printf '  exit=%s  levensduur=%ss\n' "$rc" "$((t1-t0))"; sed 's/^/    /' "$BEW/RESULT.txt"

kop "1. IS DE VARIABELE AANGEKOMEN? (uit welk pad kwamen libGLU en libtheora?)"
grep -nE 'libGLU|libtheora|libogg|geladen: 13|bibliotheken \(13\)|van de 13' "$W/log/B.out" | head -10 | sed 's/^/  /'
for l in libGLU.so.1 libtheora.so.0; do
  pad=$(grep -oE '/[^ ]*/'"$l" "$W/log/B.out" | head -1)
  case "$pad" in
    *"/lib32/"*) ok "$l kwam uit lib32 -> VARIABELE AANGEKOMEN" ;;
    *"/ElfLdr2/"*) warn "$l kwam NOG uit ElfLdr2 -> variabele NIET aangekomen" ;;
    *) warn "$l: kon het pad niet uit het logboek halen (regel: $(grep -m1 "$l" "$W/log/B.out" | cut -c1-90))" ;;
  esac
done
printf '  aantal geladen bibliotheken: %s (verwacht 13)\n' "$(grep -cE '^\s+lib.*dynsyms' "$W/log/B.out" 2>/dev/null || grep -c 'dynsyms' "$W/log/B.out")"

kop "2. VERGELIJKING MET METING 3"
if [ ! -s "$W/log/B.bestand.log" ]; then
  warn "GEEN bestandslogboek -- geen uitspraak. Zie $BEW/B.out"; head -6 "$W/log/B.out" | sed 's/^/    /'
else
  pad(){ grep FOUT "$1" 2>/dev/null | sed -n 's/^[^"]*"\([^"]*\)".*/\1/p' | sort -u; }
  pad "$BASIS/B.bestand.log" > "$W/p0.txt"; pad "$W/log/B.bestand.log" > "$W/p1.txt"
  printf '  %-22s %-12s %-12s\n' "maat" "meting 3" "lib32 eerst"
  printf '  %-22s %-12s %-12s\n' "bestandsregels" "$(wc -l < "$BASIS/B.bestand.log")" "$(wc -l < "$W/log/B.bestand.log")"
  printf '  %-22s %-12s %-12s\n' "FOUT-regels" "$(grep -c FOUT "$BASIS/B.bestand.log")" "$(grep -c FOUT "$W/log/B.bestand.log")"
  printf '  %-22s %-12s %-12s\n' "ATTACH ok" "$(grep -c 'PTRACE_ATTACH.*ok' "$BASIS/B.ptrace.log")" "$(grep -c 'PTRACE_ATTACH.*ok' "$W/log/B.ptrace.log")"
  printf '  %-22s %-12s %-12s\n' "kinderen" "$(grep -c 'kind-gemaakt' "$BASIS/B.fork.log")" "$(grep -c 'kind-gemaakt' "$W/log/B.fork.log")"
  echo "  -- nieuw --"; comm -13 "$W/p0.txt" "$W/p1.txt" | sed 's/^/      /'
  echo "  -- verdwenen --"; comm -23 "$W/p0.txt" "$W/p1.txt" | sed 's/^/      /'
fi
kop "3. DE LUS"; python3 "$D/kind-analyse.py" "$BEW" 2>&1 | sed -n '/=== 1/,/=== 5/p' | sed 's/^/  /'
kop "4. OPRUIMEN (alleen eigen PIDs) en grens"; rm -rf "$W/view" "$W/log"
for p in /g3 /T4User; do [ -e "$p" ] && echo "  LET OP: $p bestaat" || echo "  $p bestaat niet (goed)"; done
printf '  kern: %s\n  bewaard: %s\n' "$(sha256sum "$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/TeknoParrot/TeknoParrot64.dll" | cut -d' ' -f1 | cut -c1-16)" "$BEW"
