#!/bin/bash
# ts/128-maps-proef.sh - maakt de VINGERAFDRUK van arm 2 tot een GEPRINT PAD, en wel voor BEIDE ordes,
# zodat de twee armen met dezelfde methode beschreven zijn.
#
#   bash 128-maps-proef.sh            # voorbereiding (bench-vrij). START NIETS.
#   bash 128-maps-proef.sh --start    # twee korte runs (elk ~5 s).
#
# De methode: tijdens de run lees ik /proc/<pid>/maps van de gast en haal er de regels uit waar
# libGLU.so.1 en libtheora.so.0 in staan. Dat is hetzelfde middel dat de laderbouwer voor de gesloten
# route noemde, dus daarna zijn beide armen op dezelfde manier beschreven.
#
# VOORAF VASTGELEGD WAT IK VERWACHT:
#   * orde "ElfLdr2 eerst" -> in maps staat .../ElfLdr2/libs/libGLU.so.1 en .../libtheora.so.0;
#   * orde "lib32 eerst"   -> in maps staat .../runtime/lib32/libGLU.so.1 en .../libtheora.so.0.
#   * KOMT ER IETS ANDERS UIT, dan is mijn vingerafdruk-lezing van arm 2 fout en dat meld ik dan meteen.
#     Dat is precies waarom deze proef bestaat: een gePrint pad is beter dan een afgeleide.
set -u
R="$HOME/projects/ravengun"
LOADER="$R/tools/rg-elfloader/rg-elfloader"
NASDIR="/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation "
GAME="$NASDIR/game"
EL="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2"
LB32="$HOME/.local/share/ravengun/runtime/lib32"
D="$HOME/projects/ravengun-patches/patches/Terminator"
INC="$R/tools/rg-elfloader/inc"
W=/tmp/ts-maps
DO_START=0; [ "${1:-}" = "--start" ] && DO_START=1
rm -rf "$W"; mkdir -p "$W"
ok(){ printf '  OK    %s\n' "$*"; }; warn(){ printf '  LET OP %s\n' "$*"; }; kop(){ printf '\n=== %s ===\n' "$*"; }
bezet(){ local p; for p in gamescope gamescope-wl rg-elfloader wine wine64 BudgieLoader VACUUM.exe game; do
  pgrep -x "$p" >/dev/null 2>&1 && { printf '%s ' "$p"; return 0; }; done; return 1; }

kop "VOORBEREIDING"
if b=$(bezet); then warn "BEZET door: $b -- ik start NIET"; [ "$DO_START" = 1 ] && exit 1; else ok "bench vrij"; fi
for f in bestand-log.c ptracer-permit.c fork-log.c ptrace-log.c; do
  gcc -m32 -shared -fPIC -O1 -Wall -I"$INC" -o "$W/libravengun-${f%.c}.so" "$D/$f" -ldl 2>&1 | head -3
done
printf '  lader: %s\n' "$(sha256sum "$LOADER" | cut -d' ' -f1)"
if [ "$DO_START" != 1 ]; then kop "KLAAR"; echo "  Draai met: bash $0 --start"; exit 0; fi

# een run met een gekozen LD_LIBRARY_PATH-volgorde, met maps-uitlezing tijdens de run
doe_orde(){
  local naam=$1 volgorde=$2
  local w="$W/$naam"
  rm -rf "$w"; mkdir -p "$w/log" "$w/upper" "$w/work" "$w/view"
  cp "$GAME" "$w/view/game"
  for d in T4Data ServiceDongleApp hasp env irgun GeneralPurposeStateMachine.txt; do
    [ -e "$NASDIR/$d" ] && ln -sfn "$NASDIR/$d" "$w/view/$d"
  done
  [ -e "$NASDIR/T4User" ] && cp -r "$NASDIR/T4User" "$w/view/T4User"
  cp "$NASDIR/T4User/log/syslog_bb3.txt" "$w/view/syslog_bb3.txt" 2>/dev/null

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
  BW+=(--ro-bind /run /run --bind-try /run/user/1000 /run/user/1000 --bind /tmp /tmp)
  BW+=(--dir /g3 --ro-bind "$NASDIR" /g3)
  BW+=(--overlay-src "$NASDIR/T4User" --overlay "$w/upper" "$w/work" /T4User)

  cat >"$w/binnen.sh" <<EOF
cd "$w/view" || exit 1
export LD_LIBRARY_PATH="$volgorde"
export RG_ELFLOADER_VERBOSE=1
export RAVENGUN_BESTAND_LOG="$w/log/B.bestand.log"
export RAVENGUN_FORK_LOG="$w/log/B.fork.log"
export RAVENGUN_PTRACE_LOG="$w/log/B.ptrace.log"
export RAVENGUN_PTRACER_LOG="$w/log/B.permit.log"
exec timeout 60 env LD_PRELOAD="$W/libravengun-ptracer-permit.so:$W/libravengun-bestand-log.so:$W/libravengun-fork-log.so:$W/libravengun-ptrace-log.so" "$LOADER" "$w/view/game"
EOF
  printf '\n  -- orde %s: LD_LIBRARY_PATH=%s --\n' "$naam" "$volgorde"
  t0=$(date +%s)
  "${BW[@]}" /bin/bash "$w/binnen.sh" >"$w/log/B.out" 2>&1 &
  local bg=$!
  local gast="" i
  for i in $(seq 1 120); do gast=$(pgrep -x rg-elfloader 2>/dev/null | head -1); [ -n "$gast" ] && break; sleep 0.03; done
  : > "$w/maps.txt"
  if [ -n "$gast" ]; then
    printf '    gast = pid %s\n' "$gast"
    # een paar monsters tijdens de ~4 s dat hij leeft
    for i in $(seq 1 12); do
      if [ -r "/proc/$gast/maps" ]; then
        grep -E 'libGLU\.so\.1|libtheora\.so\.0' "/proc/$gast/maps" >>"$w/maps.txt" 2>/dev/null
      fi
      sleep 0.25
    done
  else
    warn "gast niet gevonden"
  fi
  wait $bg 2>/dev/null; local rc=$?; local t1=$(date +%s)
  printf '    exit=%s  levensduur=%ss  regels=%s  FOUT=%s\n' "$rc" "$((t1-t0))" \
    "$(wc -l < "$w/log/B.bestand.log" 2>/dev/null || echo 0)" "$(grep -c FOUT "$w/log/B.bestand.log" 2>/dev/null || echo 0)"
  echo "    -- de GEPRINTE paden uit /proc/<pid>/maps --"
  sort -u "$w/maps.txt" | awk '{print "      " $NF}' | sort -u | grep -E 'libGLU|libtheora' | sed 's/^/    /'
  [ -s "$w/maps.txt" ] || echo "      (geen enkele maps-regel met libGLU of libtheora gevonden)"
  # bewaren
  local bew="$D/logs-maps-$naam-$(date +%Y%m%d-%H%M)"; mkdir -p "$bew"
  cp "$w/log/"* "$bew/" 2>/dev/null; cp "$w/maps.txt" "$bew/" 2>/dev/null
  { echo "proef          : maps-uitlezing, orde $naam"
    echo "datum          : $(date -Is)"; echo "LD_LIBRARY_PATH: $volgorde"
    echo "exitcode       : $rc"; echo "levensduur     : $((t1-t0)) s"
    echo "lader sha256   : $(sha256sum "$LOADER" | cut -d' ' -f1)"
    echo "-- geprinte paden voor libGLU/libtheora --"; sort -u "$w/maps.txt" | awk '{print $NF}' | sort -u | grep -E 'libGLU|libtheora'
  } > "$bew/RESULT.txt" 2>&1
  echo "    bewaard: $bew"
  rm -rf "$w/view" "$w/log"
}

kop "TWEE KORTE RUNS: dezelfde methode voor beide ordes"
doe_orde "elflrd2-eerst" "$EL/libs:$LB32"
doe_orde "lib32-eerst"   "$LB32:$EL/libs"

kop "SAMENVATTING"
for n in elflrd2-eerst lib32-eerst; do
  f=$(ls -1dt "$D"/logs-maps-$n-* 2>/dev/null | head -1)
  [ -n "$f" ] && { echo "  -- $n --"; grep -E 'libGLU|libtheora' "$f/RESULT.txt" | sed 's/^/    /'; }
done
kop "OPRUIMEN (alleen eigen PIDs) en grens"
rm -rf "$W" 2>/dev/null
for p in /g3 /T4User; do [ -e "$p" ] && echo "  LET OP: $p bestaat" || echo "  $p bestaat niet (goed)"; done
printf '  kern: %s\n' "$(sha256sum "$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/TeknoParrot/TeknoParrot64.dll" | cut -d' ' -f1 | cut -c1-16)"
