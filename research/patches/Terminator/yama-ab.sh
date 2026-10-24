#!/bin/bash
# yama-ab.sh - DE A/B OP DE BENCH, als EEN run. Start de game twee keer; het ENIGE verschil is of de
# maatregel (libravengun-ptracer-permit.so) voorgeladen is.
#
#   bash yama-ab.sh            # preconditions + hulpmiddelen + de verwachtingen. START NIETS.
#   bash yama-ab.sh --start    # + de twee runs (elk max 90 s) + automatisch verdict. START DE GAME 2x.
#
# Arm A: alleen de WAARNEMER   (libravengun-ptrace-log.so)       -> verwacht: PTRACE_ATTACH EPERM
# Arm B: waarnemer + MAATREGEL (libravengun-ptracer-permit.so)   -> verwacht: ATTACH gelukt
#
# De waarnemer is GEEN tracer: hij wikkelt alleen de ptrace-functie in het proces zelf en laat elke
# aanroep ongewijzigd door naar de echte ptrace. Daarom is arm A het bewijs ZONDER strace (strace is
# zelf een tracer en verandert de omstandigheden: een gevolgd proces wijst PTRACE_TRACEME anders af).
#
# NAS blijft alleen-lezen: /tmp-kopie van de gast; de werkmap is een /tmp-view met symbolische
# koppelingen voor de grote leesbestanden en KOPIEEN van de bestanden waarin de gast schrijft.
#
# Onderbouwing, grensoordeel en de volledige uitkomsttabel: BESCHERMINGSLAAG-PTRACE.md
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
PLAFOND=90

ok(){   printf '  OK    %s\n' "$*"; }
bad(){  printf '  FOUT  %s\n' "$*"; }
warn(){ printf '  LET OP %s\n' "$*"; }
kop(){  printf '\n=== %s ===\n' "$*"; }

mkdir -p "$W/log"
BEWAAR="$D/logs-yama-ab-$(date +%Y%m%d-%H%M)"
rm -f "$W/log/resultaten.txt"

# ---------------------------------------------------------------- de verwachtingen, VOORAF
kop "WAT ELKE UITKOMST BETEKENT (vooraf vastgelegd; zie BESCHERMINGSLAAG-PTRACE.md §5.1)"
cat <<'TABEL'
  arm A                  arm B                        betekenis                            actie
  ---------------------  ---------------------------  -----------------------------------  ------------------------------------
  ATTACH -> EPERM        ATTACH -> 0 EN gast leeft     TREFFER: oorzaak bevestigd zonder    volgende laag in kaart brengen
                         langer of zegt iets eigens    tracer (A) en blokkade weg (B)       (PhysX of de dongle)
  ATTACH -> EPERM        ATTACH -> 0 maar gast stopt   ptrace is een SYMPTOOM, niet de      net zo hard melden; PhysX wordt
                         op DEZELFDE plek met 255      blokkade                             de volgende verdachte
  ATTACH -> EPERM        ATTACH -> EPERM ook met de    maatregel werkte niet in het juiste  eerst het permit-logboek lezen:
                         maatregel                     proces (§3.3)                        in welk proces is de prctl gezet?
  ATTACH -> 0 (al zonder ATTACH -> 0                  de Yama-hypothese is fout            luid melden; aanname opnieuw
  maatregel)
  geen ptrace-regel      geen ptrace-regel             gast neemt een ander pad             strace-meting was situationeel;
                                                                                            runs niet vergelijkbaar
  A en B identiek        A en B identiek               geen effect van de maatregel         tweede laag zoeken
TABEL
echo
echo "  TREFFER = in arm B slaagt de ATTACH EN de gast leeft langer OF zegt iets eigens."
echo "  (Een langere levensduur alleen al is een treffer: dan is de ptrace-blokkade opgeheven.)"

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
if grep -q 'wacht\[nw++\] = naam' "$R/tools/rg-elfloader/rg_elfloader.c"; then
  ok "de herkansings-lus vult ook in de gewone modus (reparatie van de laderbouwer aanwezig)"
else
  warn "de herkansings-lus lijkt nog ontleed-only; libGLU kan dan weer fataal worden"
fi

echo "-- hostbeperking --"
printf '  kernel.yama.ptrace_scope = %s (blijft ongewijzigd)\n' "$(cat /proc/sys/kernel/yama/ptrace_scope)"

echo "-- hulpmiddelen bouwen --"
gcc -m32 -pie -fPIE -O1 -Wall -I"$INC" -o "$W/yama-toets" "$D/yama-toets.c" 2>&1 | head -3
gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/libravengun-ptrace-log.so" "$D/ptrace-log.c" -ldl 2>&1 | head -3
gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/libravengun-ptracer-permit.so" "$D/ptracer-permit.c" 2>&1 | head -3
for f in yama-toets libravengun-ptrace-log.so libravengun-ptracer-permit.so; do
  [ -s "$W/$f" ] && ok "$f" || bad "$f NIET gebouwd"
done

echo "-- dezelfde A/B op systeemniveau, zonder game --"
printf '  A (geen maatregel) : '; "$W/yama-toets" 0 2>&1 | grep -o 'kind-eindstatus = .*'
printf '  B (maatregel)      : '; "$W/yama-toets" 1 2>&1 | grep -o 'kind-eindstatus = .*'

echo "-- /tmp-kopie van de gast --"
cp "$GAME" "$W/game"; csha=$(sha256sum "$W/game" | cut -d' ' -f1)
[ "$csha" = "$VERW_SHA_GAME" ] && ok "kopie in $W/game, sha256 identiek" || bad "kopie wijkt af: $csha"

if [ "$DO_START" != 1 ]; then
  kop "KLAAR VOOR DE A/B"
  echo "  Alles is voorbereid en de systeemniveau-A/B is groen."
  echo "  Draai met:  bash $0 --start        (twee runs, max ${PLAFOND}s per arm)"
  exit 0
fi

# ---------------------------------------------------------------- de runs
doe_run(){
  local arm="$1" extra="$2"
  local view="$W/view-$arm" plog="$W/log/$arm.ptrace.log" out="$W/log/$arm.out"
  rm -rf "$view"; mkdir -p "$view"
  for d in T4Data ServiceDongleApp hasp env irgun; do
    [ -e "$NASDIR/$d" ] && ln -s "$NASDIR/$d" "$view/$d"
  done
  [ -e "$NASDIR/T4User" ] && cp -r "$NASDIR/T4User" "$view/T4User"
  [ -e "$NASDIR/GeneralPurposeStateMachine.txt" ] && cp "$NASDIR/GeneralPurposeStateMachine.txt" "$view/"
  cp "$W/game" "$view/game"
  rm -f "$plog" "$W/log/$arm.permit.log"

  cat >"$W/start-$arm.sh" <<EOF
cd "$view" || exit 1
export LD_LIBRARY_PATH="$EL/libs:$LB32"
export RG_ELFLOADER_VERBOSE=1
export RAVENGUN_PTRACE_LOG="$plog"
export RAVENGUN_PTRACER_LOG="$W/log/$arm.permit.log"
exec timeout $PLAFOND env LD_PRELOAD="$W/libravengun-ptrace-log.so$extra" "$LOADER" "$view/game"
EOF
  local t0=$(date +%s)
  bash "$W/start-$arm.sh" >"$out" 2>&1
  local rc=$? t1=$(date +%s)
  # logboeken DIRECT veiligstellen, ook als de tijd ons inhaalt
  mkdir -p "$BEWAAR"
  cp "$out" "$BEWAAR/$arm.out" 2>/dev/null
  cp "$plog" "$BEWAAR/$arm.ptrace.log" 2>/dev/null
  [ -e "$W/log/$arm.permit.log" ] && cp "$W/log/$arm.permit.log" "$BEWAAR/$arm.permit.log" 2>/dev/null
  local att_ok att_err meld sprong lines
  att_ok=$(grep -c 'PTRACE_ATTACH.*ok' "$plog" 2>/dev/null || true)
  att_err=$(grep -c 'PTRACE_ATTACH.*FOUT' "$plog" 2>/dev/null || true)
  meld=$(grep -cE 'DongleInitialize\(\) FAILED|HL-RUS' "$out" 2>/dev/null || true)
  sprong=$(grep -c 'spring naar e_entry' "$out" 2>/dev/null || true)
  lines=$(wc -l <"$out" 2>/dev/null || echo 0)
  printf '%s|%s|%s|%s|%s|%s|%s|%s\n' "$arm" "$rc" "$((t1-t0))" "${att_ok:-0}" "${att_err:-0}" \
         "${meld:-0}" "${sprong:-0}" "$lines" >> "$W/log/resultaten.txt"
  printf '  arm %s: exit=%s  levensduur=%ss  ATTACH ok=%s/EPERM=%s  sprong=%s  eigen-melding=%s  logregels=%s\n' \
         "$arm" "$rc" "$((t1-t0))" "${att_ok:-0}" "${att_err:-0}" "${sprong:-0}" "${meld:-0}" "$lines"
}

kop "ARM A  alleen de waarnemer (verwacht: PTRACE_ATTACH EPERM, daarna exit 255)"
doe_run A ""
echo "  -- wat de waarnemer zag --"
grep -E 'PTRACE_ATTACH|PTRACE_TRACEME|PTRACE_DETACH' "$W/log/A.ptrace.log" 2>/dev/null | sed 's/^/    /' \
  || echo "    (geen ptrace-aanroepen gezien)"
echo "  -- waar de lader/gast stopte --"
grep -E 'spring naar e_entry|DongleInitialize|HL-RUS|FATAAL' "$W/log/A.out" 2>/dev/null | sed 's/^/    /'

kop "ARM B  waarnemer + maatregel (verwacht: ATTACH gelukt)"
doe_run B ":$W/libravengun-ptracer-permit.so"
echo "  -- wat de waarnemer zag --"
grep -E 'PTRACE_ATTACH|PTRACE_TRACEME|PTRACE_DETACH' "$W/log/B.ptrace.log" 2>/dev/null | sed 's/^/    /' \
  || echo "    (geen ptrace-aanroepen gezien)"
echo "  -- wat de maatregel zag --"
cat "$W/log/B.permit.log" 2>/dev/null | sed 's/^/    /' || echo "    (geen permit-logboek: is de maatregel geladen?)"
echo "  -- waar de lader/gast stopte --"
grep -E 'spring naar e_entry|DongleInitialize|HL-RUS|FATAAL' "$W/log/B.out" 2>/dev/null | sed 's/^/    /'

# ---------------------------------------------------------------- automatisch verdict
kop "VERDICT"
A=$(grep '^A|' "$W/log/resultaten.txt" | tail -1)
B=$(grep '^B|' "$W/log/resultaten.txt" | tail -1)
arc=$(echo "$A" | cut -d'|' -f2); adu=$(echo "$A" | cut -d'|' -f3); aaok=$(echo "$A" | cut -d'|' -f4); aaerr=$(echo "$A" | cut -d'|' -f5)
brc=$(echo "$B" | cut -d'|' -f2); bdu=$(echo "$B" | cut -d'|' -f3); baok=$(echo "$B" | cut -d'|' -f4); baerr=$(echo "$B" | cut -d'|' -f5)

printf '  %-4s exit=%-5s levensduur=%-6s ATTACH-ok=%-4s ATTACH-EPERM=%s\n' A "${arc:-?}" "${adu:-?}s" "${aaok:-0}" "${aaerr:-0}"
printf '  %-4s exit=%-5s levensduur=%-6s ATTACH-ok=%-4s ATTACH-EPERM=%s\n' B "${brc:-?}" "${bdu:-?}s" "${baok:-0}" "${baerr:-0}"
echo
if [ "${aaerr:-0}" -gt 0 ] && [ "${baok:-0}" -gt 0 ] && [ "${bdu:-0}" -gt "${adu:-0}" ]; then
  echo "  ==> TREFFER: arm A kreeg EPERM (oorzaak bevestigd ZONDER tracer) en in arm B slaagde de ATTACH"
  echo "      terwijl de gast LANGER LEEFT. De ptrace-blokkade is opgeheven."
elif [ "${aaerr:-0}" -gt 0 ] && [ "${baok:-0}" -gt 0 ]; then
  echo "  ==> DEELS: in arm B slaagt de ATTACH, maar de gast leeft niet aantoonbaar langer. Kijk in"
  echo "      $W/log/B.out of hij iets eigens zegt. Volgens §5.1 is ptrace dan een SYMPTOOM en niet de"
  echo "      blokkade -> PhysX wordt de volgende verdachte. Meld dit net zo hard."
elif [ "${aaok:-0}" -gt 0 ]; then
  echo "  ==> AFWIJKING: de ATTACH slaagde AL in arm A (zonder maatregel). De Yama-hypothese is dan fout"
  echo "      voor deze gast. Luid melden; de aanname moet opnieuw."
elif [ "${aaerr:-0}" -eq 0 ] && [ "${baerr:-0}" -eq 0 ] && [ "${aaok:-0}" -eq 0 ] && [ "${baok:-0}" -eq 0 ]; then
  echo "  ==> GEEN ptrace-aanroepen gezien: de gast neemt deze keer een ander pad. De strace-meting was"
  echo "      dan situationeel en de twee runs zijn niet vergelijkbaar."
else
  echo "  ==> ONBESLIST: zie het permit-logboek (in welk proces is de prctl gezet?) en de twee"
  echo "      uitvoerbestanden. §3.3 noemt dit als eerste verdachte."
fi

kop "BEWAARD"
echo "  Logboeken: $BEWAAR"
ls -la "$BEWAAR" | sed 's/^/    /'
echo
echo "  Trek hier GEEN conclusie over de dongle uit: de aanroepmeting van de HASP-hook"
echo "  (DRAAIBOEK-BENCH.md §7) is pas zinvol als de gast zover komt."
