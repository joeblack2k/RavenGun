#!/bin/bash
# koppel2-meting.sh - DE TWEEDE A/B: de koppeling van meting 1 BLIJFT, en daar komen twee dingen bij:
#   (1) de RELATIEVE syslog_bb3.txt in de werkmap -- een KOPIE van het NAS-bestand uit de dump zelf;
#   (2) een SYNTHETISCHE /dev/sierra2 (PTY) binnen de sandbox, zoals goedgekeurd.
#
#   bash koppel2-meting.sh            # voorbereiding + proef (bench-vrij). START NIETS.
#   bash koppel2-meting.sh --start    # de run (max 90 s). START DE GAME.
#
# WAT ANDERS IS DAN METING 1, en dat hoort zo genoteerd te worden:
#   - /dev wordt nu met `--dev /dev` (bwrap's eigen devtmpfs) opgebouwd i.p.v. `--dev-bind /dev /dev`,
#     OMDAT je in een gebonden /dev geen NIEUWE knoop kunt aanmaken (gemeten: "Can't create file
#     /dev/sierra2: Permission denied"). Daarna worden dezelfde knopen expliciet gebonden, plus
#     /dev/sierra2. Dat is nodig voor (2) en het is de enige reden voor deze wijziging.
#   - de werkmap krijgt syslog_bb3.txt als KOPIE van <spelmap>/T4User/log/syslog_bb3.txt (12.019 B).
#     Dat is bestaande inhoud uit de dump, geen verzonnen inhoud.
# De koppelingen /g3 en /T4User zijn identiek aan meting 1, en de NAS blijft read-only.
set -u

R="$HOME/projects/ravengun"
LOADER="$R/tools/rg-elfloader/rg-elfloader"
NASDIR="/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation "
GAME="$NASDIR/game"
EL="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2"
LB32="$HOME/.local/share/ravengun/runtime/lib32"
D="$HOME/projects/ravengun-patches/patches/Terminator"
INC="$R/tools/rg-elfloader/inc"
W=/tmp/ts-koppel2
DO_START=0
[ "${1:-}" = "--start" ] && DO_START=1
VERW_SHA_GAME=798dec07a71c5c37703242edab6dce6d5a7e018b8720a85986842e3ef99b206a
PLAFOND=90
VORIGE="$D/logs-koppel-20261013-1201/B.bestand.log"
mkdir -p "$W/log" "$W/upper" "$W/work" "$W/view"
ok(){ printf '  OK    %s\n' "$*"; }
warn(){ printf '  LET OP %s\n' "$*"; }
kop(){ printf '\n=== %s ===\n' "$*"; }
SOCAT=""

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
# (1) de relatieve syslog: een KOPIE uit de dump, in de werkmap
cp "$NASDIR/T4User/log/syslog_bb3.txt" "$W/view/syslog_bb3.txt" 2>/dev/null && \
  ok "syslog_bb3.txt in de werkmap gezet als kopie uit de dump ($(stat -c%s "$W/view/syslog_bb3.txt") B)" || \
  warn "geen syslog_bb3.txt in de dump gevonden om te kopieren"

# (2) de synthetische /dev/sierra2: een PTY-paar waarvan de slave als knoop wordt gebonden
if command -v socat >/dev/null 2>&1; then
  socat -d -d pty,raw,echo=0,link="$W/sierra2" pty,raw,echo=0,link="$W/sierra2-peer" >"$W/socat.log" 2>&1 &
  SOCAT=$!
  for i in $(seq 1 20); do [ -L "$W/sierra2" ] && break; sleep 0.2; done
  if [ -L "$W/sierra2" ]; then ok "PTY-paar: $W/sierra2 -> $(readlink -f "$W/sierra2") (mijn eigen socat-pid $SOCAT)"
  else warn "geen PTY gemaakt; zie $W/socat.log"; fi
else
  warn "geen socat; /dev/sierra2 wordt niet geleverd"
fi

# --- de sandbox. /dev als bwrap-eigen devtmpfs, daarna dezelfde knopen expliciet, plus /dev/sierra2.
BW=(bwrap --die-with-parent
    --ro-bind /usr /usr
    --symlink usr/lib /lib --symlink usr/lib64 /lib64 --symlink usr/bin /bin --symlink usr/bin /sbin
    --ro-bind /etc /etc --proc /proc --ro-bind /sys /sys
    --ro-bind /home /home --ro-bind /var /var --ro-bind /opt /opt --ro-bind /boot /boot --ro-bind /srv /srv)
[ -e /root ] && BW+=(--ro-bind /root /root)
BW+=(--dev /dev)
# De HOST-devpts erin, anders is /dev/pts een eigen instantie en is de socat-PTY (die op de host in
# /dev/pts/N leeft) niet bereikbaar. /dev/ptmx slaan we over: dat is in bwrap's /dev een SYMLINK naar
# pts/ptmx en daar kun je niet op mounten (gemeten: "Can't mount on symlink destination /dev/ptmx").
BW+=(--dev-bind-try /dev/pts /dev/pts)
for dn in null zero full random urandom tty shm dri snd ttyS0 input; do
  [ -e "/dev/$dn" ] && BW+=(--dev-bind-try "/dev/$dn" "/dev/$dn")
done
[ -L "$W/sierra2" ] && [ -n "$SOCAT" ] && BW+=(--dev-bind "$(readlink -f "$W/sierra2")" /dev/sierra2)
BW+=(--ro-bind /run /run --bind-try /run/user/1000 /run/user/1000 --bind /tmp /tmp)
BW+=(--dir /g3 --ro-bind "$NASDIR" /g3)
BW+=(--overlay-src "$NASDIR/T4User" --overlay "$W/upper" "$W/work" /T4User)

kop "DE PROEF (bench-vrij)"
uit=$("${BW[@]}" /bin/sh -c '
  ls /g3/T4Data/etranslation/english.csv >/dev/null 2>&1 && echo "G3:leesbaar" || echo "G3:NIET"
  head -c 20 /T4User/log/syslog_bb3.txt >/dev/null 2>&1 && echo "T4:leest-bron" || echo "T4:leest-NIET"
  echo proef > /T4User/log/t4proef.txt 2>/dev/null && echo "T4:schrijft" || echo "T4:schrijft-NIET"
  touch "/home/deck/mnt/spidernas_emulation/ts-proef-niet-mogen.txt" 2>/dev/null && echo "NAS:SCHRIJFBAAR" || echo "NAS:alleen-lezen"
  ls -l /dev/sierra2 >/dev/null 2>&1 && echo "SIERRA:aanwezig" || echo "SIERRA:afwezig"
  exec 3<>/dev/sierra2 2>/dev/null && echo "SIERRA:openbaar" || echo "SIERRA:niet-open"
  ls -l /dev/ttyS0 >/dev/null 2>&1 && echo "TTYS0:aanwezig" || echo "TTYS0:afwezig"
  cd /tmp/ts-koppel2/view && (head -c 20 syslog_bb3.txt >/dev/null 2>&1 && echo "RELSYS:leesbaar" || echo "RELSYS:NIET")
  cd /tmp/ts-koppel2/view && (echo x >> syslog_bb3.txt 2>/dev/null && echo "RELSYS:schrijfbaar" || echo "RELSYS:niet-schrijfbaar")
' 2>&1)
printf '%s\n' "$uit" | sed 's/^/  /'
for p in "G3:leesbaar" "T4:leest-bron" "T4:schrijft" "NAS:alleen-lezen" "SIERRA:aanwezig" "SIERRA:openbaar" "TTYS0:aanwezig" "RELSYS:leesbaar" "RELSYS:schrijfbaar"; do
  printf '%s' "$uit" | grep -q "$p" && ok "$p" || warn "$p ONTBREEKT"
done
[ -e "$W/upper/log/t4proef.txt" ] && ok "de T4User-schrijfactie landt LOKAAL in de upper" || warn "geen upper-schrijfactie"
[ -e "$NASDIR/T4User/log/t4proef.txt" ] && { warn "OP DE NAS!"; rm -f "$NASDIR/T4User/log/t4proef.txt"; } || ok "NIET op de NAS"

if [ "$DO_START" != 1 ]; then
  kop "KLAAR"
  echo "  Draai met: bash $0 --start"
  [ -n "$SOCAT" ] && { kill "$SOCAT" 2>/dev/null; echo "  (mijn socat-pid $SOCAT weer gestopt)"; }
  exit 0
fi

kop "DE RUN (max ${PLAFOND}s)"
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
[ -n "$SOCAT" ] && kill "$SOCAT" 2>/dev/null

BEWAAR="$D/logs-koppel2-$(date +%Y%m%d-%H%M)"; mkdir -p "$BEWAAR"; cp "$W/log/"* "$BEWAAR/" 2>/dev/null
{
  echo "run            : koppel2-meting.sh (koppeling + relatieve syslog + synthetische /dev/sierra2)"
  echo "datum          : $(date -Is)"
  echo "exitcode       : $rc"
  echo "levensduur     : $((t1-t0)) s"
  echo "lader sha256   : $(sha256sum "$LOADER" | cut -d' ' -f1)"
  echo "gast sha256    : $sha"
  echo "yama scope     : $(cat /proc/sys/kernel/yama/ptrace_scope)"
  echo "koppeling /g3  : --dir /g3 --ro-bind '$NASDIR' /g3   (zelfde als meting 1)"
  echo "koppeling /T4User: --overlay-src '$NASDIR/T4User' --overlay '$W/upper' '$W/work' /T4User   (zelfde)"
  echo "NIEUW 1        : syslog_bb3.txt in de werkmap = KOPIE van '$NASDIR/T4User/log/syslog_bb3.txt'"
  echo "NIEUW 2        : /dev/sierra2 = --dev-bind van een PTY-slave (socat, mijn eigen pid);"
  echo "                 /dev is nu --dev /dev + expliciete --dev-bind-try per knoop, want in een"
  echo "                 gebonden /dev kun je geen nieuwe knoop aanmaken"
  echo "terugdraaibaar : ja; geen hostbestand, geen hostmount; upper en PTY staan in /tmp"
  echo "bestand-regels : $( [ -f "$W/log/B.bestand.log" ] && wc -l < "$W/log/B.bestand.log" || echo 0 )"
  echo "fouten         : $( [ -f "$W/log/B.bestand.log" ] && grep -c FOUT "$W/log/B.bestand.log" || echo 0 )"
  echo "ATTACH ok      : $( [ -f "$W/log/B.ptrace.log" ] && grep -c 'PTRACE_ATTACH.*ok' "$W/log/B.ptrace.log" || echo 0 )"
} > "$BEWAAR/RESULT.txt" 2>&1
printf '  exit=%s  levensduur=%ss\n' "$rc" "$((t1-t0))"
sed 's/^/    /' "$BEWAAR/RESULT.txt"

kop "VERGELIJKING MET METING 1 (koppeling, zonder deze twee toevoegingen)"
nieuw="$BEWAAR/B.bestand.log"
if [ ! -s "$nieuw" ]; then
  warn "GEEN bestandslogboek in deze run -- er is NIETS vergeleken en er is GEEN uitspraak mogelijk."
  warn "Zie $BEWAAR/B.out"
else
  # LET OP: het pad is het EERSTE veld tussen quotes. Een gretige `.*"\([^"]*\)"` pakt het LAATSTE
  # quoted veld, en sinds de logger ook mode="rb" schrijft is dat dus "rb" -- dan lijken echte fouten
  # "opgelost" en verschijnen er spookpaden. Dat is precies gebeurd in de eerste analyse.
  pad(){ grep FOUT "$1" 2>/dev/null | sed -n 's/^[^"]*"\([^"]*\)".*/\1/p' | sort -u; }
  pad "$VORIGE" > "$W/oud.txt"; pad "$nieuw" > "$W/nieuw.txt"
  printf '  unieke ontbrekende paden: meting 1 %s, nu %s\n' "$(wc -l < "$W/oud.txt")" "$(wc -l < "$W/nieuw.txt")"
  printf '  FOUT-regels: meting 1 %s, nu %s\n' "$(grep -c FOUT "$VORIGE")" "$(grep -c FOUT "$nieuw")"
  echo "  -- WEG --"; comm -23 "$W/oud.txt" "$W/nieuw.txt" | sed 's/^/      /'
  echo "  -- NIEUW --"; comm -13 "$W/oud.txt" "$W/nieuw.txt" | sed 's/^/      /'
  echo "  -- NOG STEEDS FOUT --"; comm -12 "$W/oud.txt" "$W/nieuw.txt" | sed 's/^/      /'
  echo
  echo "  -- de resterende fouten met VLAGGEN/MODE (nu gelogd) --"
  grep FOUT "$nieuw" | grep -vE '"/tmp/ts-koppel2/log/' | sed 's/^/      /'
fi

kop "DE LUS"
python3 "$D/kind-analyse.py" "$BEWAAR" 2>&1 | sed -n '/=== 1/,/=== 5/p' | sed 's/^/  /'
kop "BEWAARD"; echo "  $BEWAAR"; ls -la "$BEWAAR" | sed 's/^/    /'
