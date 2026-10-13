#!/bin/bash
# yama-ab.sh - DE A/B OP DE BENCH. Start de game twee keer met dezelfde opzet; het ENIGE verschil is
# of libravengun-ptracer-permit.so voorgeladen is.
#
#   bash yama-ab.sh            # doet alleen de voorbereiding en de preconditions (start niets)
#   bash yama-ab.sh --start    # doet de twee runs. START DE GAME TWEE KEER.
#
# Arm A: alleen de WAARNEMER  (libravengun-ptrace-log.so)          -> verwacht: PTRACE_ATTACH EPERM
# Arm B: waarnemer + MAATREGEL (libravengun-ptracer-permit.so)     -> verwacht: ATTACH gelukt
#
# De waarnemer is GEEN tracer: hij wikkelt alleen de ptrace-functie in het proces zelf. Daarom is deze
# run het bewijs ZONDER strace (strace is zelf een tracer en verandert de omstandigheden).
#
# NAS blijft alleen-lezen: /tmp-kopie van de gast, en de werkmap is een /tmp-view met symbolische
# koppelingen voor de grote leesbestanden en KOPIEEN van de bestanden waarin de gast schrijft.
set -u

R="$HOME/projects/ravengun"
LOADER="$R/tools/rg-elfloader/rg-elfloader"
NASDIR="/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation "
GAME="$NASDIR/game"
EL="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2"
LB32="$HOME/.local/share/ravengun/runtime/lib32"
D="$HOME/projects/ravengun-patches/patches/Terminator"
INC="$R/tools/rg-elfloader/inc"
W=/tmp/ts-yama-ab
DO_START=0
[ "${1:-}" = "--start" ] && DO_START=1

VERW_GROOTTE=7764284
VERW_SHA_GAME=798dec07a71c5c37703242edab6dce6d5a7e018b8720a85986842e3ef99b206a
VERW_ENTRY=0x8057240

ok(){   printf '  OK    %s\n' "$*"; }
bad(){  printf '  FOUT  %s\n' "$*"; }
warn(){ printf '  LET OP %s\n' "$*"; }
kop(){  printf '\n=== %s ===\n' "$*"; }

mkdir -p "$W/log"

kop "VOORBEREIDING"
echo "-- bench vrij? --"
if pgrep -af 'wine|BudgieLoader|rg-elfloader|gamescope|VACUUM' 2>/dev/null | grep -v pgrep | grep -v yama-ab; then
  warn "er draait iets van een andere game"; [ "$DO_START" = 1 ] && { bad "gestopt"; exit 1; }
else ok "niets van een andere game actief"; fi

echo "-- gast --"
sz=$(stat -c%s "$GAME" 2>/dev/null || echo 0); sha=$(sha256sum "$GAME" 2>/dev/null | cut -d' ' -f1)
[ "$sz" = "$VERW_GROOTTE" ] && ok "game = $sz bytes" || bad "game = $sz bytes, verwacht $VERW_GROOTTE"
[ "$sha" = "$VERW_SHA_GAME" ] && ok "sha256 game klopt" || warn "sha256 wijkt af: $sha"

echo "-- lader --"
ls -l --time-style=+%Y-%m-%d_%H:%M:%S "$LOADER"
lsha=$(sha256sum "$LOADER" | cut -d' ' -f1); echo "  sha256: $lsha"
grep -q 'wacht\[nw++\] = naam' "$R/tools/rg-elfloader/rg_elfloader.c" \
  && ok "de herkansings-lus vult ook in de gewone modus (de reparatie van de laderbouwer)" \
  || warn "de herkansings-lus lijkt nog ontleed-only; controleer of libGLU nu wel laadt"

echo "-- hulpmiddelen bouwen --"
gcc -m32 -pie -fPIE -O1 -Wall -I"$INC" -o "$W/yama-toets" "$D/yama-toets.c" 2>&1 | head
gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/libravengun-ptrace-log.so" "$D/ptrace-log.c" -ldl 2>&1 | head
gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/libravengun-ptracer-permit.so" "$D/ptracer-permit.c" 2>&1 | head
for f in yama-toets libravengun-ptrace-log.so libravengun-ptracer-permit.so; do
  [ -s "$W/$f" ] && ok "$f gebouwd" || bad "$f NIET gebouwd"
done

echo "-- systeemniveau: dezelfde A/B, zonder game --"
printf '  A (geen maatregel): kind-eindstatus '; "$W/yama-toets" 0 2>&1 | grep -o 'kind-eindstatus = .*'
printf '  B (maatregel)     : kind-eindstatus '; "$W/yama-toets" 1 2>&1 | grep -o 'kind-eindstatus = .*'

echo "-- /tmp-kopie van de gast --"
cp "$GAME" "$W/game"; csha=$(sha256sum "$W/game" | cut -d' ' -f1)
[ "$csha" = "$VERW_SHA_GAME" ] && ok "kopie in $W/game, sha256 identiek" || bad "kopie wijkt af"

if [ "$DO_START" != 1 ]; then
  kop "KLAAR VOOR DE A/B"
  echo "  Draai met: bash $0 --start     (twee runs, elk met 90 s plafond)"
  exit 0
fi

doe_run(){   # <arm> <extra preload>
  local arm="$1" extra="$2"
  local view="$W/view-$arm" log="$W/log/$arm.ptrace.log" out="$W/log/$arm.out"
  rm -rf "$view"; mkdir -p "$view"
  for d in T4Data ServiceDongleApp hasp env irgun; do
    [ -e "$NASDIR/$d" ] && ln -s "$NASDIR/$d" "$view/$d"
  done
  [ -e "$NASDIR/T4User" ] && cp -r "$NASDIR/T4User" "$view/T4User"
  [ -e "$NASDIR/GeneralPurposeStateMachine.txt" ] && cp "$NASDIR/GeneralPurposeStateMachine.txt" "$view/"
  cp "$W/game" "$view/game"
  rm -f "$log"

  cat >"$W/start-$arm.sh" <<EOF
cd "$view" || exit 1
export LD_LIBRARY_PATH="$EL/libs:$LB32"
export RG_ELFLOADER_VERBOSE=1
export RAVENGUN_PTRACE_LOG="$log"
exec timeout 90 env LD_PRELOAD="$W/libravengun-ptrace-log.so$extra" "$LOADER" "$view/game"
EOF
  local t0=$(date +%s)
  bash "$W/start-$arm.sh" >"$out" 2>&1
  local rc=$?
  local t1=$(date +%s)
  echo "$arm|$rc|$((t1-t0))" >> "$W/log/resultaten.txt"
  printf '  arm %s: exit=%s, levensduur=%ss, log=%s\n' "$arm" "$rc" "$((t1-t0))" "$log"
}

kop "ARM A  alleen de waarnemer (verwacht: PTRACE_ATTACH EPERM, exit 255)"
doe_run A ""
echo "  -- wat de waarnemer zag --"
grep -E 'PTRACE_ATTACH|PTRACE_TRACEME|PTRACE_DETACH' "$W/log/A.ptrace.log" 2>/dev/null | sed 's/^/    /' || echo "    (geen ptrace-aanroepen gezien)"
echo "  -- waar de lader/gast stopte --"
grep -E 'spring naar e_entry|DongleInitialize|HL-RUS|FATAAL' "$W/log/A.out" | sed 's/^/    /'

kop "ARM B  waarnemer + maatregel (verwacht: ATTACH gelukt)"
doe_run B ":$W/libravengun-ptracer-permit.so"
echo "  -- wat de waarnemer zag --"
grep -E 'PTRACE_ATTACH|PTRACE_TRACEME|PTRACE_DETACH' "$W/log/B.ptrace.log" 2>/dev/null | sed 's/^/    /' || echo "    (geen ptrace-aanroepen gezien)"
echo "  -- waar de lader/gast stopte --"
grep -E 'spring naar e_entry|DongleInitialize|HL-RUS|FATAAL' "$W/log/B.out" | sed 's/^/    /'

kop "VERGELIJKING"
printf '%-6s %-8s %-12s %-10s %s\n' arm exit levensduur ATTACH "eigen melding van de gast"
for arm in A B; do
  rc=$(grep "^$arm|" "$W/log/resultaten.txt" | tail -1 | cut -d'|' -f2)
  du=$(grep "^$arm|" "$W/log/resultaten.txt" | tail -1 | cut -d'|' -f3)
  att=$(grep -c 'PTRACE_ATTACH.*ok' "$W/log/$arm.ptrace.log" 2>/dev/null || echo 0)
  ep=$(grep -c 'PTRACE_ATTACH.*FOUT' "$W/log/$arm.ptrace.log" 2>/dev/null || echo 0)
  meld=$(grep -cE 'DongleInitialize\(\) FAILED|HL-RUS' "$W/log/$arm.out" 2>/dev/null || echo 0)
  printf '%-6s %-8s %-12s %-10s %s\n' "$arm" "$rc" "${du}s" "$att ok / $ep EPERM" "$meld"
done
echo
echo "  TREFFER als: in arm B de ATTACH slaagt EN de gast langer leeft of zijn eigen melding geeft."
echo "  Zoniet: meld dat ook -- dan is de ptrace-laag niet de (enige) blokkade."
echo
echo "  Bewaar: $W/log/{A,B}.out, $W/log/{A,B}.ptrace.log"
