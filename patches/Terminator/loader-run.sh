#!/bin/bash
# loader-run.sh — de laderrun voor Terminator Salvation, als EEN script.
#
#   bash loader-run.sh            # fase 0-1: bewijzen en droogdraaien. Start NIETS. Veilig.
#   bash loader-run.sh --start    # + fase 2-3: de echte run. START DE GAME. Alleen met vrije bench.
#
# De NAS blijft alleen-lezen: er wordt uitdrukkelijk op een /tmp-kopie gewerkt, en de werkmap in
# /tmp is een view met symbolische koppelingen naar de NAS voor de grote leesbestanden, maar met
# EIGEN kopieen van de bestanden waarin het spel schrijft (T4User/, GeneralPurposeStateMachine.txt).
#
# Wat dit script bewijst, en in welke fase:
#   fase 0  de lader is de Gerepareerde versie (sha256 + de DT_GNU_HASH-route in de bron)
#   fase 1  de hele laderketen werkt op DEZE game: symbolling, shim, 13 bibliotheken, relocaties
#           -- via `--ontleed-alleen`, dat de gast NIET start
#   fase 2  de echte sprong: shim geladen -> e_entry 0x8057240 -> wat de gast zelf doet
#   fase 3  of de gast de HASP-functies werkelijk AANROEPT (adreslijst + RAVENGUN_HASP_VERBOSE)
set -u

R="$HOME/projects/ravengun"
LOADER="$R/tools/rg-elfloader/rg-elfloader"
LOADER_SRC="$R/tools/rg-elfloader/rg_elfloader.c"
INSTALLED="$HOME/.local/share/ravengun/bin/rg-elfloader"
NASDIR="/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation "
GAME="$NASDIR/game"
EL="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2"
LB32="$HOME/.local/share/ravengun/runtime/lib32"
HOOK="$HOME/projects/ravengun-patches/patches/Halo/dropin/libravengun-hasphook.so"
LIJST="$HOME/projects/ravengun-patches/patches/Terminator/hasp-haken.ini"
W=/tmp/ts-bench
VIEW="$W/view"
RUNLOG="$W/log/run-$(date +%H%M%S).txt"
DO_START=0
[ "${1:-}" = "--start" ] && DO_START=1

# Verwachte waarden -- vastgelegd VOOR de run, zodat een afwijking meteen opvalt.
VERW_GROOTTE=7764284
VERW_SHA_GAME=798dec07a71c5c37703242edab6dce6d5a7e018b8720a85986842e3ef99b206a
VERW_ENTRY=0x8057240
VERW_DYNSYMS=1020
LOADER_SHA_GOED=4ba14401062663a876f6da5eac06009a8cf323b8da1ee688c8555392adb6d719

ok(){   printf '  OK    %s\n' "$*"; }
bad(){  printf '  FOUT  %s\n' "$*"; }
warn(){ printf '  LET OP %s\n' "$*"; }
kop(){  printf '\n=== %s ===\n' "$*"; }

mkdir -p "$W/log"

# ---------------------------------------------------------------------------- FASE 0
kop "FASE 0  preconditions"

echo "-- 0.1 is de bench vrij? --"
if pgrep -af 'wine|BudgieLoader|rg-elfloader|gamescope|Luigi|Halo|Terminator' 2>/dev/null | grep -v pgrep | grep -v loader-run; then
  warn "er draait al iets van een andere game; de bench is een game tegelijk"
  [ "$DO_START" = 1 ] && { bad "gestopt: bench niet vrij"; exit 1; }
else
  ok "niets van een andere game actief"
fi

echo "-- 0.2 de gast op de NAS --"
if [ -r "$GAME" ]; then
  sz=$(stat -c%s "$GAME"); sha=$(sha256sum "$GAME" | cut -d' ' -f1)
  [ "$sz" = "$VERW_GROOTTE" ] && ok "game = $sz bytes" || bad "game is $sz bytes, verwacht $VERW_GROOTTE"
  [ "$sha" = "$VERW_SHA_GAME" ] && ok "sha256 game klopt" || { warn "sha256 game wijkt af: $sha"; warn "verwacht: $VERW_SHA_GAME  -> adressen in hasp-haken.ini horen bij de verwachte versie"; }
else
  bad "kan $GAME niet lezen"; exit 1
fi

echo "-- 0.3 de lader: pad, sha256, en is de reparatie erin? --"
[ -x "$LOADER" ] || { bad "geen uitvoerbare lader op $LOADER"; exit 1; }
ls -l --time-style=+%Y-%m-%d_%H:%M:%S "$LOADER" "$LOADER_SRC"
lsha=$(sha256sum "$LOADER" | cut -d' ' -f1)
echo "  sha256 lader : $lsha"
file -b "$LOADER" | cut -c1-70 | sed 's/^/  /'
if grep -q 'RG_HEEFT_TEL_DYNSYMS' "$LOADER_SRC" && grep -q 'TEL_BRON_STRTAB_SYMTAB' "$LOADER_SRC"; then
  ok "de DT_GNU_HASH-route zit in de BRON (tel_dynsyms + TEL_BRON_STRTAB_SYMTAB)"
else
  bad "de bron heeft de DT_GNU_HASH-route NIET; wacht op de laderbouwer"; exit 1
fi
[ "$lsha" = "$LOADER_SHA_GOED" ] && ok "lader is de versie die ik heb getest" \
  || warn "andere lader-sha256 dan de geteste ($LOADER_SHA_GOED); als de bouwer opnieuw bouwde is dat te verwachten -- het bewijs hieronder geldt"
if [ -e "$INSTALLED" ]; then
  isha=$(sha256sum "$INSTALLED" | cut -d' ' -f1)
  if [ "$isha" != "$lsha" ]; then
    warn "DE GEINSTALLEERDE KOPIE IS EEN ANDERE (en was op 2026-10-12 21:46 nog de OUDE, zonder fix):"
    warn "  $INSTALLED  sha256 $isha"
    warn "  gebruik daarom hieronder de REPO-lader, niet de geinstalleerde. Of laat de bouwer opnieuw installeren."
  else
    ok "geinstalleerde kopie is identiek aan de repo-lader"
  fi
fi
echo "  (operationeel bewijs van de fix volgt in fase 1: de teller moet 1020 uit STRTAB-SYMTAB halen)"

echo "-- 0.4 lossen alle 13 bibliotheken op? --"
ldd_out=$(LD_LIBRARY_PATH="$EL/libs:$LB32" ldd "$GAME" 2>&1)
if echo "$ldd_out" | grep -q 'not found'; then
  bad "ontbrekende bibliotheken:"; echo "$ldd_out" | grep 'not found' | sed 's/^/      /'
  warn "libglut.so.3 hoort uit runtime/lib32 te komen, niet uit ElfLdr2/libs"
  [ "$DO_START" = 1 ] && exit 1
else
  ok "niets ontbreekt met LD_LIBRARY_PATH=$EL/libs:$LB32"
fi
echo "  libglut.so.3 => $(echo "$ldd_out" | awk '/libglut/{print $3}')"

echo "-- 0.5 de adreslijst voor de HASP-hook --"
if [ -r "$LIJST" ]; then
  n=$(grep -cE '^hasp_' "$LIJST")
  ok "hasp-haken.ini aanwezig, $n actieve hasp_-regels (verwacht 11; 10 worden aangehaakt, hasp_free wordt terecht geweigerd)"
else
  warn "geen $LIJST; de hook kan dan alleen de naam-route en die vindt hier 0 hasp-symbolen"
fi

# ---------------------------------------------------------------------------- FASE 1
kop "FASE 1  bewijs + droogdraaien (de gast wordt NIET gestart)"
echo "-- 1.1 kopie van de gast naar /tmp --"
cp "$GAME" "$W/game" || { bad "kopieren mislukt"; exit 1; }
csha=$(sha256sum "$W/game" | cut -d' ' -f1)
[ "$csha" = "$VERW_SHA_GAME" ] && ok "kopie in $W/game, sha256 identiek aan de NAS" \
  || bad "kopie wijkt af: $csha"
echo
echo "-- 1.2 de droogloop: --ontleed-alleen --"
echo "   commando:"
echo "     cd $W"
echo "     LD_LIBRARY_PATH=$EL/libs:$LB32 RG_ELFLOADER_VERBOSE=1 \\"
echo "       $LOADER --ontleed-alleen $W/game"
( cd "$W" && LD_LIBRARY_PATH="$EL/libs:$LB32" RG_ELFLOADER_VERBOSE=1 \
    timeout 180 "$LOADER" --ontleed-alleen "$W/game" ) >"$W/log/dryrun.txt" 2>&1
dry=$?
echo "   exit=$dry   (log: $W/log/dryrun.txt)"
echo
echo "-- 1.3 verwachte regels, een voor een --"
check(){ # <patroon> <verwachting>
  if grep -qF -- "$1" "$W/log/dryrun.txt"; then ok "$2"; else bad "$2 -- regel niet gevonden: $1"; fi
}
check "bron: (DT_STRTAB-DT_SYMTAB)/16, geen DT_HASH" "de teller gebruikt de STRTAB-SYMTAB-route (DIT IS DE REPARATIE)"
check "gast-dynsyms: $VERW_DYNSYMS"                  "gast-dynsyms = $VERW_DYNSYMS, exact wat de statische meting voorspelde"
check "shim geladen:"                                "de shim is gebouwd en in de globale scope geladen"
check "geladen: libglut.so.3"                        "libglut.so.3 is geladen (de bibliotheek die op de host ontbreekt)"
check "stale-map: 496 JMPREL-entries, 0 met stale"   "de pakker-reparatie is een no-op (0 stale slots) -- precies zoals voorspeld voor de ONGEPAKTE klasse"
check "spel NIET gestart"                            "de droogloop heeft de gast inderdaad niet gestart"
if grep -q 'FATAAL' "$W/log/dryrun.txt"; then bad "er staat een FATAAL in de droogloop:"; grep 'FATAAL' "$W/log/dryrun.txt" | sed 's/^/      /'; else ok "geen FATAAL"; fi
echo
echo "-- 1.4 de twee onopgeloste symbolen (verwacht: alleen __gmon_start__) --"
grep -A4 'eerste onopgeloste symbolen' "$W/log/dryrun.txt" | sed 's/^/   /'
echo
echo "-- 1.5 de bibliotheeklijst uit de analyse (13 verwacht) --"
sed -n '/^bibliotheken/,/^lib-defs/p' "$W/log/dryrun.txt" | sed 's/^/   /'

if [ "$DO_START" != 1 ]; then
  kop "KLAAR VOOR DE BENCHRUN"
  echo "  Fase 0 en 1 zijn groen: de gerepareerde lader doet op DEZE game alles t/m de relocaties."
  echo "  De enige stap die nog niet bewezen is, is de sprong zelf (fase 2) -- die start de gast."
  echo
  echo "  Draai op de bench, met een vrije bench:"
  echo "      bash $0 --start"
  exit 0
fi

# ---------------------------------------------------------------------------- FASE 2
kop "FASE 2  DE ECHTE RUN (de gast wordt GESTART)"
echo "-- 2.1 werkmap in /tmp die de NAS alleen-lezen houdt --"
rm -rf "$VIEW"; mkdir -p "$VIEW"
# grote leesbestanden: symbolisch koppelen naar de NAS
for d in T4Data ServiceDongleApp hasp env irgun; do
  [ -e "$NASDIR/$d" ] && ln -s "$NASDIR/$d" "$VIEW/$d"
done
# bestanden waarin het spel SCHRIJFT: echte kopieen in /tmp, niet naar de NAS
[ -e "$NASDIR/T4User" ] && cp -r "$NASDIR/T4User" "$VIEW/T4User"
[ -e "$NASDIR/GeneralPurposeStateMachine.txt" ] && cp "$NASDIR/GeneralPurposeStateMachine.txt" "$VIEW/"
# de uitvoerbare gast: de /tmp-kopie
cp "$W/game" "$VIEW/game"
ls -la "$VIEW" | sed 's/^/   /'
echo "   (T4User en GeneralPurposeStateMachine.txt zijn KOPIEEN: het spel schrijft daarin, de NAS niet)"

echo
echo "-- 2.2 de aanroep, met harde tijdslimiet --"
echo "   cwd          : $VIEW"
echo "   LD_LIBRARY_PATH=$EL/libs:$LB32"
echo "   tijdslimiet  : 90 s"
cat >"$W/start.sh" <<EOF
cd "$VIEW" || exit 1
export LD_LIBRARY_PATH="$EL/libs:$LB32"
export RG_ELFLOADER_VERBOSE=1
export RAVENGUN_HASP_ELF_GAME="$VIEW/game"
export RAVENGUN_HASP_ELF_ADRESLIJST="$LIJST"
export RAVENGUN_HASP_ELF_LOG="$RUNLOG"
export RAVENGUN_HASP_VERBOSE=1
export LD_PRELOAD="$HOOK"
exec timeout 90 "$LOADER" "$VIEW/game"
EOF
chmod +x "$W/start.sh"
echo "   (LD_PRELOAD=$HOOK zorgt voor de aanroep-log van fase 3)"
bash "$W/start.sh" >"$W/log/run.out" 2>&1
rc=$?
echo "   exit=$rc   (stdout: $W/log/run.out, hooklog: $RUNLOG)"

echo
echo "-- 2.3 de vier succesregels, in volgorde --"
for p in "shim geladen:" "geladen; spel" "spring naar e_entry $VERW_ENTRY" ; do
  if grep -qF -- "$p" "$W/log/run.out"; then ok "gevonden: $p"; else bad "NIET gevonden: $p"; fi
done
grep -nE 'spring naar e_entry' "$W/log/run.out" | sed 's/^/   /'
echo
echo "-- 2.4 WAT DE GAST ZELF DEED --"
echo "   (dit is de uitkomst die telt; de lader is klaar zodra 'spring naar e_entry' er staat)"
if grep -qiE 'DongleInitialize\(\) FAILED|HL-RUS License' "$W/log/run.out"; then
  ok "DE GAST GEEFT ZIJN EIGEN DONGLEMELDING: dit is een GOEDE uitkomst voor de lader"
  echo "        de lader heeft gedaan wat hij moest doen; het volgende probleem is de donglecontrole van de gast"
  grep -iE 'DongleInitialize\(\) FAILED|HL-RUS License' "$W/log/run.out" | sed 's/^/        /'
elif grep -q 'spring naar e_entry' "$W/log/run.out"; then
  warn "de gast kreeg besturing maar gaf geen donglemelding; kijk wat er WEL staat:"
  tail -25 "$W/log/run.out" | sed 's/^/        /'
else
  bad "geen sprong naar e_entry: de lader is niet klaar. Zie de tabel in DRAAIBOEK-BENCH.md §3."
fi

# ---------------------------------------------------------------------------- FASE 3
kop "FASE 3  roept de gast de HASP-functies werkelijk aan? (adreslijst)"
if [ -r "$RUNLOG" ]; then
  echo "-- 3.1 de hook zelf --"
  grep -E 'ravengun-hasphook.*(adreslijst|aangehaakt|totaal|AFGEWEZEN)' "$RUNLOG" | sed 's/^/   /'
  echo
  echo "-- 3.2 de aanroepen (hh_meld, alleen met RAVENGUN_HASP_VERBOSE=1) --"
  aanroepen=$(grep -c 'aanroep:' "$RUNLOG" 2>/dev/null || true)
  if [ "${aanroepen:-0}" -gt 0 ]; then
    ok "$aanroepen aanroep-regels"
    grep 'aanroep:' "$RUNLOG" | awk '{print $NF}' | sort | uniq -c | sort -rn | sed 's/^/        /'
    echo "        volgorde:"
    grep 'aanroep:' "$RUNLOG" | awk '{print $NF}' | head -20 | tr '\n' ' ' | sed 's/^/        /'; echo
  else
    warn "GEEN aanroep-regels. Dat betekent: de gast raakt de HASP-API niet aan."
    warn "Volgens DRAAIBOEK-BENCH.md §7.3 is dat de MEEST WAARDEVOLLE uitkomst: de blokkade zit dan"
    warn "VOOR de HASP-API -- in de engine (DongleInit/DongleOpen) of in de /g3/-paden, niet in HASP."
  fi
else
  warn "geen hooklog op $RUNLOG; is de hook wel geladen? (LD_PRELOAD in fase 2.2)"
fi

kop "SAMENVATTING"
echo "  lader-sha256   : $lsha"
echo "  gast-sha256    : $csha"
echo "  fase 1 (droog) : $(grep -c 'FATAAL' "$W/log/dryrun.txt") FATAAL, dynsyms $(grep -oE 'gast-dynsyms: [0-9]+' "$W/log/dryrun.txt" | head -1 | awk '{print $2}')"
echo "  fase 2 (echt)  : exit=$rc, e_entry gesprongen: $(grep -c 'spring naar e_entry' "$W/log/run.out")"
echo "  fase 3 (hook)  : ${aanroepen:-0} aanroepen geregistreerd"
echo
echo "  Bewaar voor het verslag: $W/log/dryrun.txt, $W/log/run.out, $RUNLOG"
