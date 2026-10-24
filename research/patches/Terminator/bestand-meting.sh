#!/bin/bash
# bestand-meting.sh - DE BESTANDSMETING: welke bestanden opent de gast NA de sprong, en welke mislukken?
#
#   bash bestand-meting.sh            # voorbereiding + de uitkomsttabel. START NIETS.
#   bash bestand-meting.sh --start    # de run (max 90 s). START DE GAME.
#   RG_DROOG='sleep 2; exit 42' bash bestand-meting.sh --start
#                                     # DROOGLOOP: geen game; draait een stub zodat alleen de
#                                     # boekhouding (exitcode/levensduur/RESULT.txt) getest wordt.
#
# WAAROM GEEN strace: strace is zelf een tracer en laat de gast op zijn EIGEN ptrace-poort sterven
# (BESCHERMINGSLAAG-PTRACE.md §8.1). Met strace bereik je het venster met de bestandsopeningen nooit.
# Deze waarnemer kijkt vanuit het proces zelf mee (LD_PRELOAD) en verandert niets.
#
# De maatregel moet MEE: zonder PR_SET_PTRACER_ANY sterft de gast op de poort voordat hij iets opent.
set -u

R="$HOME/projects/ravengun"
LOADER="$R/tools/rg-elfloader/rg-elfloader"
NASDIR="/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation "
GAME="$NASDIR/game"
EL="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2"
LB32="$HOME/.local/share/ravengun/runtime/lib32"
D="$HOME/projects/ravengun-patches/patches/Terminator"
INC="$R/tools/rg-elfloader/inc"
W=/tmp/ts-bestand
DO_START=0
[ "${1:-}" = "--start" ] && DO_START=1
VERW_SHA_GAME=798dec07a71c5c37703242edab6dce6d5a7e018b8720a85986842e3ef99b206a
PLAFOND=90
mkdir -p "$W/log"
BEWAAR="$D/logs-bestand-$(date +%Y%m%d-%H%M)"
ok(){ printf '  OK    %s\n' "$*"; }
warn(){ printf '  LET OP %s\n' "$*"; }
kop(){ printf '\n=== %s ===\n' "$*"; }

kop "WAT ELKE UITKOMST BETEKENT (vooraf vastgelegd)"
cat <<'TABEL'
  waarneming                                   betekenis                          actie
  -------------------------------------------  ---------------------------------  ----------------------------------
  "PhysX.conf" -> FOUT errno=2 (No such file)
        -> de gast zoekt dat bestand en vindt het niet. Dat is de eerste concrete
           kandidaat voor de blokkade. Eerst VASTSTELLEN waar hij het zoekt (het pad
           staat in de regel) en wat hij ermee doet -- niet gokken en niet zelf een
           bestand verzinnen.

  een "/g3/..."-pad -> FOUT errno=2
        -> het absolute dataroot van de engine ontbreekt op deze host (gemeten: /g3
           bestaat niet). Dat bevestigt de eerdere verdenking. De oplossing is dan een
           HOST-inrichting (het pad leveren), geen wijziging aan de gast.

  alle openingen slagen, geen enkele ENOENT
        -> de blokkade zit NIET in een bestand. Dan is de volgende verdachte de engine-
           logica zelf (DongleInit/GetDongleID) of een socket/ioctl. Niet blijven
           zoeken in bestanden die er wel zijn.

  dezelfde regel tientallen keren herhaald (zelfde pad, zelfde uitkomst)
        -> de gast POLLT op dat pad. Dat verklaart de gepacede lus van 11 ms uit de
           vorige meting (BESCHERMINGSLAAG-PTRACE.md §8.5): elk kind is een tik van die
           poll. Het herhaalde pad is dan het antwoord -- dat is wat hij wil hebben.

  bijna geen regels (weinig openingen)
        -> de gast sterft voordat hij iets van zichzelf aanraakt; dan zit de blokkade
           niet in bestanden maar eerder in de opstart. Vergelijk met de lus.

  geen logboek
        -> de waarnemer is niet geladen; niets interpreteren voor dat klopt.
TABEL
echo "  (per bevinding wordt GEMETEN of AFGELEID gemeld; zie ook het verdict onderaan)"

kop "VOORBEREIDING"
if pgrep -x gamescope >/dev/null 2>&1 || pgrep -x rg-elfloader >/dev/null 2>&1 || pgrep -x wine >/dev/null 2>&1; then
  warn "er draait al iets van een andere game"; [ "$DO_START" = 1 ] && exit 1
else ok "bench vrij"; fi
sha=$(sha256sum "$GAME" | cut -d' ' -f1)
[ "$sha" = "$VERW_SHA_GAME" ] && ok "sha256 gast klopt" || warn "sha256 wijkt af: $sha"
printf '  lader: %s\n' "$(sha256sum "$LOADER" | cut -d' ' -f1)"
for f in bestand-log.c ptracer-permit.c fork-log.c ptrace-log.c; do
  out="libravengun-${f%.c}.so"
  gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/$out" "$D/$f" -ldl 2>&1 | head -3
  [ -s "$W/$out" ] && ok "$out" || warn "$out NIET gebouwd"
done
cp "$GAME" "$W/game"

if [ "$DO_START" != 1 ]; then
  kop "KLAAR"; echo "  Draai met: bash $0 --start"; exit 0
fi

kop "DE RUN (max ${PLAFOND}s)"
VIEW="$W/view"; rm -rf "$VIEW"; mkdir -p "$VIEW"
for d in T4Data ServiceDongleApp hasp env irgun; do
  [ -e "$NASDIR/$d" ] && ln -s "$NASDIR/$d" "$VIEW/$d"
done
[ -e "$NASDIR/T4User" ] && cp -r "$NASDIR/T4User" "$VIEW/T4User"
[ -e "$NASDIR/GeneralPurposeStateMachine.txt" ] && cp "$NASDIR/GeneralPurposeStateMachine.txt" "$VIEW/"
cp "$W/game" "$VIEW/game"

# Het te draaien commando. Met RG_DROOG draait er GEEN game maar een stub: dat oefent de boekhouding
# (exitcode, levensduur, RESULT.txt) zonder de bench te gebruiken.
if [ -n "${RG_DROOG:-}" ]; then
  CMD="sh -c '$RG_DROOG'"
  warn "DROOGLOOP actief: er wordt GEEN game gestart ($RG_DROOG)"
else
  CMD="\"$LOADER\" \"$VIEW/game\""
fi

cat >"$W/start.sh" <<EOF
cd "$VIEW" || exit 1
export LD_LIBRARY_PATH="$EL/libs:$LB32"
export RG_ELFLOADER_VERBOSE=1
export RAVENGUN_BESTAND_LOG="$W/log/B.bestand.log"
export RAVENGUN_FORK_LOG="$W/log/B.fork.log"
export RAVENGUN_PTRACE_LOG="$W/log/B.ptrace.log"
export RAVENGUN_PTRACER_LOG="$W/log/B.permit.log"
exec timeout $PLAFOND env LD_PRELOAD="$W/libravengun-ptracer-permit.so:$W/libravengun-bestand-log.so:$W/libravengun-fork-log.so:$W/libravengun-ptrace-log.so" $CMD
EOF
t0=$(date +%s); bash "$W/start.sh" >"$W/log/B.out" 2>&1; rc=$?; t1=$(date +%s)
mkdir -p "$BEWAAR"; cp "$W/log/"* "$BEWAAR/" 2>/dev/null
# De samenvatting NAAR SCHIJF, zodat een afgekapte terminal de exitcode niet kan laten verdwijnen.
{
  echo "run            : bestand-meting.sh"
  echo "datum          : $(date -Is)"
  echo "droogloop      : ${RG_DROOG:-nee}"
  echo "exitcode       : $rc"
  echo "levensduur     : $((t1-t0)) s"
  echo "lader sha256   : $(sha256sum "$LOADER" | cut -d' ' -f1)"
  echo "gast sha256    : $sha"
  echo "yama scope     : $(cat /proc/sys/kernel/yama/ptrace_scope)"
  echo "bestand-regels : $( [ -f "$W/log/B.bestand.log" ] && wc -l < "$W/log/B.bestand.log" || echo 0 )"
  echo "fouten (ENOENT): $( [ -f "$W/log/B.bestand.log" ] && grep -c 'FOUT' "$W/log/B.bestand.log" || echo 0 )"
  echo "ATTACH ok      : $( [ -f "$W/log/B.ptrace.log" ] && grep -c 'PTRACE_ATTACH.*ok' "$W/log/B.ptrace.log" || echo 0 )"
} > "$BEWAAR/RESULT.txt" 2>&1
printf '  exit=%s  levensduur=%ss\n' "$rc" "$((t1-t0))"
echo "  -- RESULT.txt (dit is de boekhouding die eerder ontbrak) --"
cat "$BEWAAR/RESULT.txt" | sed 's/^/    /'

echo
echo "-- 1. hoeveel bestandsaanroepen, en welke MISLUKTEN --"
n=$(wc -l < "$W/log/B.bestand.log" 2>/dev/null || echo 0)
printf '    totaal: %s regels\n' "$n"
grep 'FOUT' "$W/log/B.bestand.log" 2>/dev/null | sed 's/^/    /' | head -25
echo
echo "-- 2. speciaal: PhysX.conf --"
if grep -q 'PhysX.conf' "$W/log/B.bestand.log" 2>/dev/null; then
  grep 'PhysX.conf' "$W/log/B.bestand.log" | sed 's/^/    /' | head -10
else
  echo "    PhysX.conf komt in GEEN enkele aanroep voor"
fi
echo
echo "-- 3. speciaal: /g3/ paden --"
if grep -q '"/g3' "$W/log/B.bestand.log" 2>/dev/null; then
  grep '"/g3' "$W/log/B.bestand.log" | sed 's/^/    /' | head -15
else
  echo "    geen enkel /g3-pad aangeraakt"
fi
echo
echo "-- 4. de meest herhaalde paden (pollt de gast ergens op?) --"
sed -n 's/.*"\([^"]*\)".*/\1/p' "$W/log/B.bestand.log" 2>/dev/null | sort | uniq -c | sort -rn | head -10 | sed 's/^/    /'
echo
echo "-- 5. waar stopte de gast --"
grep -nE 'spring naar e_entry|FATAAL|DongleInitialize|HL-RUS' "$W/log/B.out" | sed 's/^/    /'

kop "VERDICT"
if [ ! -s "$W/log/B.bestand.log" ] && [ -z "${RG_DROOG:-}" ]; then
  echo "  ==> GEEN bestandslogboek: de waarnemer is niet geladen. Niets interpreteren."
elif [ -n "${RG_DROOG:-}" ]; then
  echo "  ==> DROOGLOOP: geen game. Alleen bewezen dat de boekhouding (exitcode/levensduur/RESULT.txt)"
  echo "      werkt; de bestandsregels zijn van de stub, niet van de gast."
else
  enoent=$(grep -c 'FOUT errno=2' "$W/log/B.bestand.log" 2>/dev/null || true)
  ph=$(grep -c 'PhysX.conf' "$W/log/B.bestand.log" 2>/dev/null || true)
  g3=$(grep -c '"/g3' "$W/log/B.bestand.log" 2>/dev/null || true)
  top=$(sed -n 's/.*"\([^"]*\)".*/\1/p' "$W/log/B.bestand.log" 2>/dev/null | sort | uniq -c | sort -rn | head -1)
  echo "  mislukte openingen (ENOENT): ${enoent:-0} van ${n} regels"
  echo "  PhysX.conf: ${ph:-0} keer genoemd;  /g3-paden: ${g3:-0} keer"
  if [ "${enoent:-0}" -gt 0 ]; then
    echo "  ==> ER ZIJN ONTBREKENDE BESTANDEN. Zie lijst 1 hierboven: dat zijn de kandidaten."
    echo "      Volgens de tabel: eerst VASTSTELLEN waar en waarvoor, niet zelf een bestand verzinnen."
  else
    echo "  ==> GEEN ontbrekend bestand. De blokkade zit dan NIET in een bestand: volgende verdachte is"
    echo "      de engine-logica zelf (DongleInit/GetDongleID) of de socket/ioctl-kant."
  fi
  [ -n "$top" ] && echo "  meest herhaalde pad: $top  (bij een hoge teller: de gast POLLT daarop)"
fi

kop "BEWAARD"; echo "  $BEWAAR"; ls -la "$BEWAAR" | sed 's/^/    /'
