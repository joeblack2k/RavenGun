#!/bin/bash
# hasp-meting.sh - DE AANROEPMETING. Een enkele run, nu voor het eerst zinvol: de gast komt voorbij de
# ptrace-poort (bewezen: 390 van 390 ATTACH's in de A/B van 2026-10-13), dus als hij de HASP-API NIET
# aanraakt, is dat een geldige conclusie in plaats van "we zijn er nooit geweest".
#
#   bash hasp-meting.sh            # voorbereiding + de uitkomsttabel printen. START NIETS.
#   bash hasp-meting.sh --start    # + de run (max 90 s). START DE GAME.
#
# Opzet: de maatregel MOET mee (anders sterft de gast op de poort, zie BESCHERMINGSLAAG-PTRACE.md),
# plus de HASP-hook met RAVENGUN_HASP_VERBOSE=1 (die schrijft "aanroep: <naam>" per aanroep), plus de
# twee waarnemers (ptrace + fork) zodat we ook weten of de gast werkte of spinde.
#
# NAS blijft alleen-lezen: /tmp-kopie van de gast en een /tmp-view met symlinks voor de grote
# leesbestanden en KOPIEEN van de bestanden waarin de gast schrijft.
set -u

R="$HOME/projects/ravengun"
LOADER="$R/tools/rg-elfloader/rg-elfloader"
NASDIR="/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation "
GAME="$NASDIR/game"
EL="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2"
LB32="$HOME/.local/share/ravengun/runtime/lib32"
D="$HOME/projects/ravengun-patches/patches/Terminator"
HOOK="$HOME/projects/ravengun-patches/patches/Halo/dropin/libravengun-hasphook.so"
LIJST="$D/hasp-haken.ini"
INC="$R/tools/rg-elfloader/inc"
W=/tmp/ts-hasp
DO_START=0
[ "${1:-}" = "--start" ] && DO_START=1

VERW_GROOTTE=7764284
VERW_SHA_GAME=798dec07a71c5c37703242edab6dce6d5a7e018b8720a85986842e3ef99b206a
PLAFOND=90

ok(){   printf '  OK    %s\n' "$*"; }
bad(){  printf '  FOUT  %s\n' "$*"; }
warn(){ printf '  LET OP %s\n' "$*"; }
kop(){  printf '\n=== %s ===\n' "$*"; }
mkdir -p "$W/log"; rm -f "$W/log/B."*.log
BEWAAR="$D/logs-hasp-meting-$(date +%Y%m%d-%H%M)"

# ---------------------------------------------------------------- de verwachtingen, VOORAF
kop "WAT ELKE UITKOMST BETEKENT (vooraf vastgelegd)"
cat <<'TABEL'
  waarneming                                  betekenis                              actie
  ------------------------------------------  -------------------------------------  ----------------------------------
  hook keurt 10 GELDIG / 1 AFGEWEZEN (hasp_free) EN >=1 "aanroep:"-regel
        -> de gast ROEPT de HASP-API aan. De namen en de VOLGORDE zijn de spec voor de emulator.
           Dit is de uitkomst die het donglewerk concreet maakt: per aangeroepen functie weet je
           wat er beantwoord moet worden.

  hook gekeurd, MAAR 0 "aanroep:"-regels EN de gast kwam voorbij de poort
        (>=1 geslaagde PTRACE_ATTACH in het waarnemer-logboek)
        -> de blokkade zit VOOR de HASP-API: in de engine (DongleInit/DongleOpen/GetAladdinHLPass)
           of in de /g3/-paden. DIT IS NU EEN GELDIGE CONCLUSIE, want de gast is aantoonbaar
           voorbij de poort gekomen. Het werk verschuift van "HASP emuleren" naar die twee.

  hook gekeurd, 0 "aanroep:", EN 0 geslaagde ATTACH
        -> de gast kwam deze run NIET voorbij de poort; niet vergelijkbaar. Eerst de maatregel
           controleren (permit-logboek) voor je hier iets uit concludeert.

  exit 255 met "DongleInitialize() FAILED" EN 0 "aanroep:"
        -> de gast bereikte de DONGLE-code van de engine maar riep de ruwe HASP-API nooit aan.
           Dan faalt de engine dus in zijn EIGEN wikkel (DongleInit/GetAladdinHLPass) of op een
           bestand dat hij daar verwacht -- niet in de HASP-API zelf.

  GEEN "adreslijst:"-regel in het hooklogboek
        -> de hook is niet geladen; eerst LD_PRELOAD/HASP_ELF_ADRESLIJST nakijken. Niets uit deze
           run interpreteren voordat dat klopt.
TABEL

kop "VOORBEREIDING"
echo "-- bench vrij? --"
if pgrep -x gamescope >/dev/null 2>&1 || pgrep -x rg-elfloader >/dev/null 2>&1 || pgrep -x wine >/dev/null 2>&1; then
  warn "er draait al iets van een andere game"; [ "$DO_START" = 1 ] && { bad "gestopt"; exit 1; }
else ok "niets van een andere game actief"; fi

echo "-- gast en lader --"
sz=$(stat -c%s "$GAME" 2>/dev/null || echo 0); sha=$(sha256sum "$GAME" 2>/dev/null | cut -d' ' -f1)
[ "$sz" = "$VERW_GROOTTE" ] && ok "game = $sz bytes" || bad "game = $sz bytes, verwacht $VERW_GROOTTE"
[ "$sha" = "$VERW_SHA_GAME" ] && ok "sha256 game klopt" || warn "sha256 wijkt af: $sha"
printf '  lader: %s  %s\n' "$(sha256sum "$LOADER" | cut -d' ' -f1)" "$(stat -c%s "$LOADER") bytes"

echo "-- de hook en de adreslijst --"
[ -e "$HOOK" ] && ok "hook: $HOOK ($(stat -c%s "$HOOK") bytes)" || bad "hook ontbreekt"
[ -e "$LIJST" ] && ok "adreslijst: $LIJST ($(grep -cE '^hasp_' "$LIJST") actieve regels)" || bad "adreslijst ontbreekt"

echo "-- hostbeperking --"
printf '  kernel.yama.ptrace_scope = %s (blijft ongewijzigd; de maatregel is per proces)\n' \
       "$(cat /proc/sys/kernel/yama/ptrace_scope)"

echo "-- hulpmiddelen bouwen --"
gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/libravengun-ptrace-log.so"     "$D/ptrace-log.c" -ldl 2>&1 | head -3
gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/libravengun-fork-log.so"       "$D/fork-log.c" -ldl 2>&1 | head -3
gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/libravengun-ptracer-permit.so" "$D/ptracer-permit.c" 2>&1 | head -3
for f in libravengun-ptrace-log.so libravengun-fork-log.so libravengun-ptracer-permit.so; do
  [ -s "$W/$f" ] && ok "$f" || bad "$f NIET gebouwd"
done
cp "$GAME" "$W/game"
[ "$(sha256sum "$W/game" | cut -d' ' -f1)" = "$VERW_SHA_GAME" ] && ok "kopie in $W/game, sha256 identiek" || bad "kopie wijkt af"

if [ "$DO_START" != 1 ]; then
  kop "KLAAR VOOR DE AANROEPMETING"
  echo "  Draai met: bash $0 --start     (één run, max ${PLAFOND}s)"
  exit 0
fi

# ---------------------------------------------------------------- de run
kop "DE RUN (de gast wordt gestart, max ${PLAFOND}s)"
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
export RAVENGUN_PTRACE_LOG="$W/log/B.ptrace.log"
export RAVENGUN_FORK_LOG="$W/log/B.fork.log"
export RAVENGUN_PTRACER_LOG="$W/log/B.permit.log"
export RAVENGUN_HASP_ELF_GAME="$VIEW/game"
export RAVENGUN_HASP_ELF_ADRESLIJST="$LIJST"
export RAVENGUN_HASP_ELF_LOG="$W/log/B.hasp.log"
export RAVENGUN_HASP_VERBOSE=1
# de maatregel MOET mee (anders sterft de gast op de poort); daarna de hook en de twee waarnemers
if [ -n "${RG_DROOG:-}" ]; then
  exec timeout $PLAFOND env LD_PRELOAD="$W/libravengun-ptracer-permit.so" sh -c '$RG_DROOG'
fi
exec timeout $PLAFOND env LD_PRELOAD="$W/libravengun-ptracer-permit.so:$W/libravengun-fork-log.so:$W/libravengun-ptrace-log.so:$HOOK" "$LOADER" "$VIEW/game"
EOF
t0=$(date +%s); bash "$W/start.sh" >"$W/log/B.out" 2>&1; rc=$?; t1=$(date +%s)
mkdir -p "$BEWAAR"; cp "$W/log/"* "$BEWAAR/" 2>/dev/null
# De samenvatting NAAR SCHIJF: een afgekapte terminal mag de exitcode niet laten verdwijnen. Dat is
# precies wat er bij de eerste run misging, dus dit is de reparatie daarvan.
{
  echo "run            : hasp-meting.sh"
  echo "datum          : $(date -Is)"
  echo "droogloop      : ${RG_DROOG:-nee}"
  echo "exitcode       : $rc"
  echo "levensduur     : $((t1-t0)) s"
  echo "lader sha256   : $(sha256sum "$LOADER" | cut -d' ' -f1)"
  echo "gast sha256    : $sha"
  echo "yama scope     : $(cat /proc/sys/kernel/yama/ptrace_scope)"
  echo "aanroep-regels : $(grep -c 'aanroep:' "$W/log/B.hasp.log" 2>/dev/null || true)"
  echo "ATTACH ok      : $(grep -c 'PTRACE_ATTACH.*ok' "$W/log/B.ptrace.log" 2>/dev/null || true)"
} > "$BEWAAR/RESULT.txt" 2>&1
printf '  exit=%s  levensduur=%ss\n' "$rc" "$((t1-t0))"
echo "  -- RESULT.txt --"; cat "$BEWAAR/RESULT.txt" | sed 's/^/    /'
echo
echo "-- 1. de hook (keuring + aanhaken) --"
grep -E 'adreslijst|GELDIG|AFGEWEZEN|aangehaakt|totaal aangehaakt' "$W/log/B.hasp.log" 2>/dev/null | sed 's/^/    /' \
  || echo "    GEEN hook-logboek: de hook is niet geladen (zie de tabel hierboven)"
echo
echo "-- 2. DE AANROEPEN (dit is de meting) --"
n=$(grep -c 'aanroep:' "$W/log/B.hasp.log" 2>/dev/null || true)
printf '    aantal "aanroep:"-regels: %s\n' "${n:-0}"
if [ "${n:-0}" -gt 0 ]; then
  grep 'aanroep:' "$W/log/B.hasp.log" | awk '{print $NF}' | uniq -c | sed 's/^/    /'
  echo "    volgorde:"
  grep 'aanroep:' "$W/log/B.hasp.log" | awk '{print $NF}' | head -25 | tr '\n' ' ' | sed 's/^/      /'; echo
fi
echo
echo "-- 3. kwam de gast voorbij de poort? --"
att=$(grep -c 'PTRACE_ATTACH.*ok' "$W/log/B.ptrace.log" 2>/dev/null || true)
attf=$(grep -c 'PTRACE_ATTACH.*FOUT' "$W/log/B.ptrace.log" 2>/dev/null || true)
printf '    geslaagde ATTACH: %s   EPERM: %s   (poort voorbij = ja als >0 geslaagd)\n' "${att:-0}" "${attf:-0}"
echo
echo "-- 4. waar stopte de gast --"
grep -nE 'spring naar e_entry|FATAAL|DongleInitialize|HL-RUS|License|PhysX|error|Error' "$W/log/B.out" | sed 's/^/    /'
echo
echo "-- 5. werkte de gast of spinde hij? (kinderen) --"
python3 "$D/kind-analyse.py" "$W/log" 2>&1 | sed -n '/=== 4/,$p' | sed 's/^/  /'

kop "VERDICT"
if ! grep -q 'adreslijst' "$W/log/B.hasp.log" 2>/dev/null; then
  echo "  ==> HOOK NIET GELADEN. Niets uit deze run interpreteren; eerst LD_PRELOAD nakijken."
elif [ "${n:-0}" -gt 0 ]; then
  echo "  ==> DE GAST ROEPT DE HASP-API AAN (${n} aanroepen). De namen en de volgorde hierboven zijn de"
  echo "      spec voor de emulator: per aangeroepen functie is nu bekend wat er beantwoord moet worden."
elif [ "${att:-0}" -gt 0 ]; then
  echo "  ==> 0 AANROEPEN EN TOCH VOORBIJ DE POORT (${att} geslaagde ATTACH's). Dit is de geldige"
  echo "      conclusie: de blokkade zit VOOR de HASP-API -- in de engine (DongleInit/DongleOpen/"
  echo "      GetAladdinHLPass) of in de /g3/-paden. Het donglewerk is daarmee NIET de volgende stap."
else
  echo "  ==> 0 AANROEPEN EN NIET VOORBIJ DE POORT: niet vergelijkbaar. Controleer eerst het"
  echo "      permit-logboek; zonder geslaagde ATTACH zegt deze run niets over de HASP-API."
fi

kop "BEWAARD"
echo "  Logboeken: $BEWAAR"
ls -la "$BEWAAR" | sed 's/^/    /'
