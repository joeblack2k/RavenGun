#!/bin/bash
# ts/132-display-proef.sh - DE DISPLAYSLOT. Een variabele t.o.v. arm 1: HET SCHERM.
#   run A: de argumenten (-n -f1360x768) MET de schermomgeving van de tv
#   run B: de CONTROLE, dezelfde run ZONDER argumenten -- alleen zo kun je het scherm van de argumenten
#          onderscheiden. Zelfde slot, zodat de vergelijking schoon is.
#
#   bash 132-display-proef.sh            # voorbereiding + schermcontrole. START NIETS.
#   bash 132-display-proef.sh --start    # de twee runs.
#
# VOORAF VASTGELEGD:
#   * Als de argumenten iets doen VOOR HET BEELD, dan hoort het volledig-schermverzoek nu te worden
#     ingewilligd en gedraagt de gast zich anders dan in arm 1.
#   * Gedraagt hij zich IDENTIEK terwijl er een scherm is, dan is dat een STERKERE nul dan arm 1.
#   * Ik kan zelf niet op de tv kijken: ik leg het beeld vast met `spectacle` en beoordeel de opname.
#     Lukt vastleggen niet, dan zeg ik dat en vraag ik de eigenaar te kijken -- ik verzin geen beeld.
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
W=/tmp/ts-display
DO_START=0; [ "${1:-}" = "--start" ] && DO_START=1
ARGUMENTEN=(-n -f1360x768)
rm -rf "$W"; mkdir -p "$W"
ok(){ printf '  OK    %s\n' "$*"; }; warn(){ printf '  LET OP %s\n' "$*"; }; kop(){ printf '\n=== %s ===\n' "$*"; }
bezet(){ local p; for p in gamescope gamescope-wl rg-elfloader wine wine64 BudgieLoader VACUUM.exe game; do
  pgrep -x "$p" >/dev/null 2>&1 && { printf '%s ' "$p"; return 0; }; done; return 1; }

kop "VOORBEREIDING"
if b=$(bezet); then warn "BEZET door: $b -- ik start NIET"; [ "$DO_START" = 1 ] && exit 1; else ok "bench vrij"; fi
[ -x "$TV" ] && ok "rg-tv-env.sh gevonden" || { warn "rg-tv-env.sh NIET gevonden"; exit 1; }

kop "SCHERMCONTROLE (--check; eindigt met 1 als er geen bereikbaar scherm is)"
if "$TV" --check; then ok "het scherm is bereikbaar"; else warn "rg-tv-env.sh --check FAALT (exit $?) -- ik STOP hier, want een run zou arm 1 met een ander label zijn"; exit 1; fi

for f in bestand-log.c ptracer-permit.c fork-log.c ptrace-log.c; do
  gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/libravengun-${f%.c}.so" "$D/$f" -ldl 2>&1 | head -3
done
printf '  lader: %s\n' "$(sha256sum "$LOADER" | cut -d' ' -f1)"
if [ "$DO_START" != 1 ]; then kop "KLAAR"; echo "  Draai met: bash $0 --start"; exit 0; fi

doe_run(){
  local naam=$1; shift
  local args=("$@")
  local w="$W/$naam"; rm -rf "$w"; mkdir -p "$w/log" "$w/upper" "$w/work" "$w/view"
  cp "$GAME" "$w/view/game"
  for d in T4Data ServiceDongleApp hasp env irgun GeneralPurposeStateMachine.txt; do
    [ -e "$NASDIR/$d" ] && ln -sfn "$NASDIR/$d" "$w/view/$d"
  done
  [ -e "$NASDIR/T4User" ] && cp -r "$NASDIR/T4User" "$w/view/T4User"
  cp "$NASDIR/T4User/log/syslog_bb3.txt" "$w/view/syslog_bb3.txt" 2>/dev/null
  socat -d -d pty,raw,echo=0,link="$w/sierra2" pty,raw,echo=0,link="$w/peer" >"$w/socat.log" 2>&1 &
  local soc=$!; for i in $(seq 1 20); do [ -L "$w/sierra2" ] && break; sleep 0.2; done

  local -a BW=(bwrap --die-with-parent
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
exec timeout 45 env LD_PRELOAD="$W/libravengun-ptracer-permit.so:$W/libravengun-bestand-log.so:$W/libravengun-fork-log.so:$W/libravengun-ptrace-log.so" "$LOADER" "$w/view/game" ${args[*]:-}
EOF
  printf '\n  -- run %s (argumenten: %s) --\n' "$naam" "${args[*]:-geen}"
  local t0=$(date +%s)
  "$TV" -- /bin/bash "$w/binnen.sh" >"$w/log/B.out" 2>&1 &
  local bg=$!
  local i; for i in 1 2 3 4 5 6; do
    sleep 0.8
    "$TV" -- spectacle -b -n -f -o "$w/shot-$i.png" >/dev/null 2>&1 || true
    [ -s "$w/shot-$i.png" ] && break
  done
  wait $bg 2>/dev/null; local rc=$?; local t1=$(date +%s)
  kill "$soc" 2>/dev/null
  printf '    exit=%s  levensduur=%ss  regels=%s  FOUT=%s\n' "$rc" "$((t1-t0))" \
    "$(wc -l < "$w/log/B.bestand.log" 2>/dev/null || echo 0)" "$(grep -c FOUT "$w/log/B.bestand.log" 2>/dev/null || echo 0)"
  grep -nE 'spring naar e_entry|FATAAL|GLX|glX|X11|display' "$w/log/B.out" | head -6 | sed 's/^/      /'
  echo "    -- opnamen --"
  for s in "$w"/shot-*.png; do [ -f "$s" ] && printf '      %s  %s bytes\n' "$(basename "$s")" "$(stat -c%s "$s")"; done
  local bew="$D/logs-display-$naam-$(date +%Y%m%d-%H%M)"; mkdir -p "$bew"
  cp "$w/log/"* "$bew/" 2>/dev/null; cp "$w"/shot-*.png "$bew/" 2>/dev/null
  { echo "run            : $naam (argumenten: ${args[*]:-geen})"; echo "datum          : $(date -Is)"
    echo "exitcode       : $rc"; echo "levensduur     : $((t1-t0)) s"
    echo "lader sha256   : $(sha256sum "$LOADER" | cut -d' ' -f1)"
    echo "regels         : $(wc -l < "$w/log/B.bestand.log" 2>/dev/null || echo 0)"
    echo "FOUT           : $(grep -c FOUT "$w/log/B.bestand.log" 2>/dev/null || echo 0)"
    echo "ATTACH ok      : $(grep -c 'PTRACE_ATTACH.*ok' "$w/log/B.ptrace.log" 2>/dev/null || echo 0)"
    echo "-- opnamen --"; for s in "$bew"/shot-*.png; do [ -f "$s" ] && echo "$(basename "$s") $(stat -c%s "$s")"; done
  } > "$bew/RESULT.txt"
  echo "    bewaard: $bew"
  rm -rf "$w/view" "$w/log"
}

kop "RUN A -- MET de argumenten, MET scherm"; doe_run "met-argumenten" "${ARGUMENTEN[@]}"
kop "RUN B -- CONTROLE: ZONDER argumenten, MET scherm"; doe_run "zonder-argumenten"

kop "SAMENVATTING"
for n in met-argumenten zonder-argumenten; do
  f=$(ls -1dt "$D"/logs-display-$n-* 2>/dev/null | head -1)
  [ -n "$f" ] && { echo "  -- $n --"; sed 's/^/    /' "$f/RESULT.txt"; }
done

kop "OPRUIMEN (alleen eigen PIDs) en grens"; rm -rf "$W" 2>/dev/null
for p in /g3 /T4User; do [ -e "$p" ] && echo "  LET OP: $p bestaat" || echo "  $p bestaat niet (goed)"; done
printf '  kern: %s\n' "$(sha256sum "$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/TeknoParrot/TeknoParrot64.dll" | cut -d' ' -f1 | cut -c1-16)"
