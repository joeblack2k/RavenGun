#!/bin/bash
# kinderen-meting.sh - DE KINDEREN METEN, zonder de HASP-hook.
#
#   bash kinderen-meting.sh            # voorbereiding. START NIETS.
#   bash kinderen-meting.sh --start    # de run (max 90 s). START DE GAME.
#
# Doel: is het patroon van de 392 kinderen uit de A/B een WERKERPOOL of een LUS? De HASP-hook wordt
# hier BEWUST weggelaten: die zet tien inline-patchen in het beeld en kan de timing beinvloeden. Zo is
# deze meting een onafhankelijke herhaling van het kindpatroon, met alleen de maatregel + de waarnemers.
#
# Uitkomst wordt beoordeeld door kind-analyse.py, dat op twee proefprocessen met BEKENDE vorm is
# getest (30 kinderen/4 ms -> "geen lus"; 200 kinderen/~0 ms -> "LUS").
set -u

R="$HOME/projects/ravengun"
LOADER="$R/tools/rg-elfloader/rg-elfloader"
NASDIR="/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation "
GAME="$NASDIR/game"
EL="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2"
LB32="$HOME/.local/share/ravengun/runtime/lib32"
D="$HOME/projects/ravengun-patches/patches/Terminator"
INC="$R/tools/rg-elfloader/inc"
W=/tmp/ts-kinderen
DO_START=0
[ "${1:-}" = "--start" ] && DO_START=1

VERW_SHA_GAME=798dec07a71c5c37703242edab6dce6d5a7e018b8720a85986842e3ef99b206a
PLAFOND=90
mkdir -p "$W/log"
BEWAAR="$D/logs-kinderen-$(date +%Y%m%d-%H%M)"
ok(){ printf '  OK    %s\n' "$*"; }
warn(){ printf '  LET OP %s\n' "$*"; }
kop(){ printf '\n=== %s ===\n' "$*"; }

kop "VOORBEREIDING"
if pgrep -x gamescope >/dev/null 2>&1 || pgrep -x rg-elfloader >/dev/null 2>&1 || pgrep -x wine >/dev/null 2>&1; then
  warn "er draait al iets van een andere game"; [ "$DO_START" = 1 ] && exit 1
else ok "bench vrij"; fi
sha=$(sha256sum "$GAME" | cut -d' ' -f1)
[ "$sha" = "$VERW_SHA_GAME" ] && ok "sha256 gast klopt" || warn "sha256 wijkt af: $sha"
printf '  lader: %s\n' "$(sha256sum "$LOADER" | cut -d' ' -f1)"
gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/libravengun-fork-log.so"       "$D/fork-log.c" -ldl 2>&1 | head -3
gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/libravengun-ptrace-log.so"     "$D/ptrace-log.c" -ldl 2>&1 | head -3
gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/libravengun-ptracer-permit.so" "$D/ptracer-permit.c" 2>&1 | head -3
for f in libravengun-fork-log.so libravengun-ptrace-log.so libravengun-ptracer-permit.so; do
  [ -s "$W/$f" ] && ok "$f" || warn "$f NIET gebouwd"
done
cp "$GAME" "$W/game"

if [ "$DO_START" != 1 ]; then
  kop "KLAAR"; echo "  Draai met: bash $0 --start"; exit 0
fi

kop "DE RUN (max ${PLAFOND}s, geen HASP-hook)"
VIEW="$W/view"; rm -rf "$VIEW"; mkdir -p "$VIEW"
for d in T4Data ServiceDongleApp hasp env irgun; do
  [ -e "$NASDIR/$d" ] && ln -s "$NASDIR/$d" "$VIEW/$d"
done
[ -e "$NASDIR/T4User" ] && cp -r "$NASDIR/T4User" "$VIEW/T4User"
[ -e "$NASDIR/GeneralPurposeStateMachine.txt" ] && cp "$NASDIR/GeneralPurposeStateMachine.txt" "$VIEW/"
cp "$W/game" "$VIEW/game"

cat >"$W/start.sh" <<EOF
cd "$VIEW" || exit 1
export LD_LIBRARY_PATH="$EL/libs:$LB32"
export RG_ELFLOADER_VERBOSE=1
export RAVENGUN_FORK_LOG="$W/log/B.fork.log"
export RAVENGUN_PTRACE_LOG="$W/log/B.ptrace.log"
export RAVENGUN_PTRACER_LOG="$W/log/B.permit.log"
exec timeout $PLAFOND env LD_PRELOAD="$W/libravengun-ptracer-permit.so:$W/libravengun-fork-log.so:$W/libravengun-ptrace-log.so" "$LOADER" "$VIEW/game"
EOF
t0=$(date +%s); bash "$W/start.sh" >"$W/log/B.out" 2>&1; rc=$?; t1=$(date +%s)
mkdir -p "$BEWAAR"; cp "$W/log/"* "$BEWAAR/" 2>/dev/null
# Het kopsamenvatting ook NAAR SCHIJF: een afgekapte terminal mag de exitcode niet laten verdwijnen.
{ echo "run            : kinderen-meting.sh (geen HASP-hook)"
  echo "datum          : $(date -Is)"
  echo "exitcode       : $rc"
  echo "levensduur     : $((t1-t0)) s"
  echo "lader sha256   : $(sha256sum "$LOADER" | cut -d' ' -f1)"
  echo "gast sha256    : $sha"
  echo "yama scope     : $(cat /proc/sys/kernel/yama/ptrace_scope)"
  echo "ATTACH ok/EPERM: $(grep -c 'PTRACE_ATTACH.*ok' "$W/log/B.ptrace.log" 2>/dev/null || echo 0)/$(grep -c 'PTRACE_ATTACH.*FOUT' "$W/log/B.ptrace.log" 2>/dev/null || echo 0)"
} > "$BEWAAR/RESULT.txt" 2>&1
printf '  exit=%s  levensduur=%ss\n' "$rc" "$((t1-t0))"
echo
echo "-- de maatregel --"; cat "$W/log/B.permit.log" 2>/dev/null | sed 's/^/    /'
echo "-- ptrace: geslaagd/EPERM --"
printf '    ATTACH ok=%s  EPERM=%s  DETACH=%s  TRACEME=%s\n' \
  "$(grep -c 'PTRACE_ATTACH.*ok' "$W/log/B.ptrace.log" 2>/dev/null || echo 0)" \
  "$(grep -c 'PTRACE_ATTACH.*FOUT' "$W/log/B.ptrace.log" 2>/dev/null || echo 0)" \
  "$(grep -c 'PTRACE_DETACH' "$W/log/B.ptrace.log" 2>/dev/null || echo 0)" \
  "$(grep -c 'PTRACE_TRACEME' "$W/log/B.ptrace.log" 2>/dev/null || echo 0)"
echo "-- waar de gast stopte --"
grep -nE 'spring naar e_entry|FATAAL|DongleInitialize|HL-RUS|PhysX' "$W/log/B.out" | sed 's/^/    /'
echo
echo "-- eerste 5 en laatste 3 regels van het fork-logboek --"
head -5 "$W/log/B.fork.log" 2>/dev/null | sed 's/^/    /'
echo "    ..."
tail -3 "$W/log/B.fork.log" 2>/dev/null | sed 's/^/    /'
echo
kop "KIND-ANALYSE"
python3 "$D/kind-analyse.py" "$W/log" 2>&1 | sed 's/^/  /'
kop "BEWAARD"
echo "  $BEWAAR"; ls -la "$BEWAAR" | sed 's/^/    /'
