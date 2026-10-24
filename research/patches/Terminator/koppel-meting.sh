#!/bin/bash
# koppel-meting.sh - DE A/B: dezelfde bestandsmeting, maar MET de twee koppelingen.
#
#   bash koppel-meting.sh            # voorbereiding + de koppelproef (bench-vrij). START NIETS.
#   bash koppel-meting.sh --start    # de run (max 90 s). START DE GAME.
#
# WAT ER GEKOPPELD WORDT (en dat is het ENIGE verschil met de vorige meting):
#   /g3        --ro-bind   <spelmap op de NAS>              (lezen; de gast vroeg hierom)
#   /T4User    --overlay   NAS-T4User als onderlaag,
#                          upper + work LOKAAL in /tmp      (schrijven blijft lokaal, NIET op de NAS)
# De rest is identiek: dezelfde waarnemers, dezelfde werkmap-opzet, geen --unshare-* vlaggen, dus
# de procesboom, het netwerk en de pid's blijven zoals in de vorige meting. EEN variabele.
#
# HARDE VOORWAARDE (eigenaar): de NAS blijft alleen-lezen. In deze sandbox is /home --ro-bind, dus de
# gast KAN niet naar de NAS schrijven; de koppelproef hieronder bewijst dat, en bewijst ook dat een
# schrijfactie in /T4User in de lokale upper landt.
#
# TERUGDRAAIBAAR: er verandert niets op de host. Geen /g3 en geen /T4User op het echte root, geen
# mount die blijft staan. De koppelingen bestaan alleen binnen deze ene run; de upper staat in /tmp en
# mag weg. Er is geen host- of NAS-bestand aangemaakt.
set -u

R="$HOME/projects/ravengun"
LOADER="$R/tools/rg-elfloader/rg-elfloader"
NASDIR="/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation "
GAME="$NASDIR/game"
EL="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2"
LB32="$HOME/.local/share/ravengun/runtime/lib32"
D="$HOME/projects/ravengun-patches/patches/Terminator"
INC="$R/tools/rg-elfloader/inc"
W=/tmp/ts-koppelmeting
DO_START=0
[ "${1:-}" = "--start" ] && DO_START=1
VERW_SHA_GAME=798dec07a71c5c37703242edab6dce6d5a7e018b8720a85986842e3ef99b206a
PLAFOND=90
VORIGE="$D/logs-bestand-20261013-1150/B.bestand.log"
mkdir -p "$W/log" "$W/upper" "$W/work" "$W/view"
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
for f in bestand-log.c ptracer-permit.c fork-log.c ptrace-log.c; do
  out="libravengun-${f%.c}.so"
  gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/$out" "$D/$f" -ldl 2>&1 | head -3
  [ -s "$W/$out" ] && ok "$out" || warn "$out NIET gebouwd"
done
cp "$GAME" "$W/view/game"
for d in T4Data ServiceDongleApp hasp env irgun GeneralPurposeStateMachine.txt; do
  [ -e "$NASDIR/$d" ] && ln -sfn "$NASDIR/$d" "$W/view/$d"
done
[ -e "$NASDIR/T4User" ] && cp -r "$NASDIR/T4User" "$W/view/T4User"

# --- de sandbox-root, zoals de route hem opbouwt (play-native.sh r185-190). LET OP: /lib, /lib64,
#     /bin en /sbin zijn op dit systeem SYMLINKS naar usr/...; die moet je als --symlink overnemen en
#     NIET als --ro-bind van de inhoud, anders klopt de relatieve keten van de 32-bits interpreter niet
#     (/usr/lib/ld-linux.so.2 -> ../lib32/ld-linux.so.2, en die /lib32 bestaat in de sandbox niet).
#     Dat was precies waarom de eerste poging met exit 127 stierf: env kon de 32-bits lader niet starten.
#     En: GEEN --ro-bind / /, want dan kan bwrap /g3 en /T4User niet aanmaken (gemeten: EROFS).
BW=(bwrap --die-with-parent
    --ro-bind /usr /usr
    --symlink usr/lib /lib --symlink usr/lib64 /lib64 --symlink usr/bin /bin --symlink usr/bin /sbin
    --ro-bind /etc /etc --proc /proc --dev-bind /dev /dev --ro-bind /sys /sys
    --ro-bind /home /home --ro-bind /var /var --ro-bind /opt /opt --ro-bind /boot /boot --ro-bind /srv /srv)
[ -e /root ] && BW+=(--ro-bind /root /root)
BW+=(--ro-bind /run /run --bind-try /run/user/1000 /run/user/1000)
BW+=(--bind /tmp /tmp)
BW+=(--dir /g3 --ro-bind "$NASDIR" /g3)
BW+=(--overlay-src "$NASDIR/T4User" --overlay "$W/upper" "$W/work" /T4User)

kop "DE KOPPELPROEF (bench-vrij, bewijst de harde voorwaarde)"
uit=$("${BW[@]}" /bin/sh -c '
  ls /g3/T4Data/etranslation/english.csv >/dev/null 2>&1 && echo "G3:leesbaar" || echo "G3:NIET"
  head -c 20 /T4User/log/syslog_bb3.txt >/dev/null 2>&1 && echo "T4:leest-NAS-bron" || echo "T4:leest-NIET"
  echo proef > /T4User/log/koppel-proef.txt 2>/dev/null && echo "T4:schrijft" || echo "T4:schrijft-NIET"
  touch "/home/deck/mnt/spidernas_emulation/ts-proef-niet-mogen.txt" 2>/dev/null && echo "NAS:SCHRIJFBAAR" || echo "NAS:alleen-lezen"
' 2>&1)
printf '%s\n' "$uit" | sed 's/^/  /'
printf '%s' "$uit" | grep -q 'G3:leesbaar' && ok "/g3 leest de spelmap" || warn "/g3 leest niet"
printf '%s' "$uit" | grep -q 'NAS:alleen-lezen' && ok "de NAS is in de sandbox alleen-lezen" || warn "LET OP: NAS lijkt schrijfbaar!"
if [ -e "$W/upper/log/koppel-proef.txt" ]; then ok "schrijven in /T4User landt LOKAAL in de upper"; else warn "geen lokale upper-schrijfactie gezien"; fi
if [ -e "$NASDIR/T4User/log/koppel-proef.txt" ]; then warn "OP DE NAS!"; rm -f "$NASDIR/T4User/log/koppel-proef.txt"; else ok "NIET op de NAS"; fi

if [ "$DO_START" != 1 ]; then
  kop "KLAAR"; echo "  Draai met: bash $0 --start"; exit 0
fi

kop "DE RUN MET DE KOPPELING (max ${PLAFOND}s)"
cat >"$W/binnen.sh" <<EOF
cd "$W/view" || exit 1
export LD_LIBRARY_PATH="$EL/libs:$LB32"
export RG_ELFLOADER_VERBOSE=1
export RAVENGUN_BESTAND_LOG="$W/log/B.bestand.log"
export RAVENGUN_FORK_LOG="$W/log/B.fork.log"
export RAVENGUN_PTRACE_LOG="$W/log/B.ptrace.log"
export RAVENGUN_PTRACER_LOG="$W/log/B.permit.log"
exec timeout $PLAFOND env LD_PRELOAD="$W/libravengun-ptracer-permit.so:$W/libravengun-bestand-log.so:$W/libravengun-fork-log.so:$W/libravengun-ptrace-log.so" "$LOADER" "$W/view/game"
EOF
chmod +x "$W/binnen.sh"
t0=$(date +%s); "${BW[@]}" /bin/bash "$W/binnen.sh" >"$W/log/B.out" 2>&1; rc=$?; t1=$(date +%s)

BEWAAR="$D/logs-koppel-$(date +%Y%m%d-%H%M)"; mkdir -p "$BEWAAR"
cp "$W/log/"* "$BEWAAR/" 2>/dev/null
{
  echo "run            : koppel-meting.sh (A/B met de twee koppelingen)"
  echo "datum          : $(date -Is)"
  echo "exitcode       : $rc"
  echo "levensduur     : $((t1-t0)) s"
  echo "lader sha256   : $(sha256sum "$LOADER" | cut -d' ' -f1)"
  echo "gast sha256    : $sha"
  echo "yama scope     : $(cat /proc/sys/kernel/yama/ptrace_scope)"
  echo "koppeling /g3  : --dir /g3 --ro-bind '$NASDIR' /g3"
  echo "koppeling /T4User: --overlay-src '$NASDIR/T4User' --overlay '$W/upper' '$W/work' /T4User"
  echo "sandbox        : bwrap --die-with-parent, per top-level pad --ro-bind, --bind /tmp,"
  echo "                 --ro-bind /home (= NAS onbereikbaar voor schrijven), GEEN --unshare-*"
  echo "terugdraaibaar : ja; niets op de host of de NAS aangeraakt, upper staat in /tmp"
  echo "bestand-regels : $( [ -f "$W/log/B.bestand.log" ] && wc -l < "$W/log/B.bestand.log" || echo 0 )"
  echo "fouten         : $( [ -f "$W/log/B.bestand.log" ] && grep -c FOUT "$W/log/B.bestand.log" || echo 0 )"
  echo "ATTACH ok      : $( [ -f "$W/log/B.ptrace.log" ] && grep -c 'PTRACE_ATTACH.*ok' "$W/log/B.ptrace.log" || echo 0 )"
} > "$BEWAAR/RESULT.txt" 2>&1
printf '  exit=%s  levensduur=%ss\n' "$rc" "$((t1-t0))"
sed 's/^/    /' "$BEWAAR/RESULT.txt"

kop "VERGELIJKING MET DE VORIGE METING (zonder koppeling)"
nieuw="$BEWAAR/B.bestand.log"
# HARD: vergelijk alleen als de run echt iets geproduceerd heeft. Anders meldt comm alle oude fouten
# als "weg" terwijl er niets gebeurd is -- dat is precies de valse succesmelding die ik wil vermijden.
if [ ! -s "$nieuw" ]; then
  warn "DE NIEUWE RUN HEEFT GEEN BESTANDSLOGBOEK OPGELEVERD (0 regels)."
  warn "Er is dus NIETS vergeleken en er is GEEN uitspraak over de koppeling mogelijk."
  warn "Kijk in $BEWAAR/B.out waarom de run niet startte."
  padlijst(){ :; }
else
padlijst(){ grep FOUT "$1" 2>/dev/null | sed -n 's/.*"\([^"]*\)".*/\1/p' | sort -u; }
padlijst "$VORIGE" > "$W/oud.txt"; padlijst "$nieuw" > "$W/nieuw.txt"
printf '  unieke ontbrekende paden: vorige %s, nu %s\n' "$(wc -l < "$W/oud.txt")" "$(wc -l < "$W/nieuw.txt")"
echo "  -- WEG (nu gevonden, waren FOUT) --"; comm -23 "$W/oud.txt" "$W/nieuw.txt" | sed 's/^/      /' || echo "      (geen)"
echo "  -- NIEUW (stonden niet in de vorige lijst) --"; comm -13 "$W/oud.txt" "$W/nieuw.txt" | sed 's/^/      /' || echo "      (geen)"
echo "  -- NOG STEEDS FOUT --"; comm -12 "$W/oud.txt" "$W/nieuw.txt" | sed 's/^/      /' || echo "      (geen)"
echo
printf '  regels: vorige %s, nu %s\n' "$(wc -l < "$VORIGE")" "$(wc -l < "$nieuw")"
echo "  (vorige was 2424 waarvan 2336 eigen instrumentatie; nu schrijven de waarnemers met rauwe syscalls,"
echo "   dus dit getal is het ECHTE aantal)"
fi

kop "DE GEPACEDE LUS: veranderd of niet?"
if [ -f "$nieuw" ] || [ -f "$BEWAAR/B.fork.log" ]; then
  python3 "$D/kind-analyse.py" "$BEWAAR" 2>&1 | sed -n '/=== 1/,/=== 5/p' | sed 's/^/  /'
else
  warn "geen logboeken om te analyseren"
fi
kop "BEWAARD"; echo "  $BEWAAR"; ls -la "$BEWAAR" | sed 's/^/    /'
