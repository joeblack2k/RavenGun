#!/usr/bin/env bash
# b0-vijfmin.sh -- de 5-minutenproef voor Luigi's Mansion op ONZE lader.
#
# DOEL (eigenaar): het spel draait 5 MINUTEN op onze eigen lader. Tot nu toe: 288 s met de
# GESLOTEN kern, 6,3 s met onze kern. Deze proef is de meetlat.
#
# WAT HET DOET
#   * de gewone werkende route: `launch-direct.py LuigisMansion --run` (die zet prefix, werkmap
#     van de lader, hulpketen en omgeving goed), headless gamescope zodat het scherm vrij blijft;
#   * de kern-DLL die je opgeeft (standaard onze huidige build) wordt tijdelijk op zijn plaats
#     gezet en daarna GEGARANDEERD teruggezet, met sha256-controle;
#   * elke 30 s game-tijd een regel: game-tijd, leeft het, threads, entry-point-staat
#     (trampoline of teruggezet), .text-vulling, en hoeveel bestanden van het spel openstaan;
#   * harde limiet standaard 480 s, stopgrens 300 s game-tijd;
#   * NAS-schrijfcontrole over het hele venster (mtime/hash voor en na);
#   * per run de kern-sha256 zoals launch-direct hem ZELF in zijn log schrijft;
#   * opruimen: procesgroep kill -9, resten wegvegen, runtime-kern terug op f5c7532d.
#
# SYSTEEM-UNIT: dit script zet zichzelf in een systemd-user-unit als het niet al onder systemd
# loopt, zodat het een ssh-sessie overleeft. Volgen: journalctl --user -u <unit> -f
#
# SUCCESCRITERIA (vooraf, zie ook B0-VIJFMIN.md)
#   1. 300 s game-tijd gehaald (stopgrens bereikt met een levend proces)
#   2. entry point TERUGGEZET op het stopmoment (eb 08), niet de trampoline
#   3. .text gevuld (ruim onder 100% nul; de gesloten kern zit op 11,2%)
#   4. threads ruim boven 1 (de gesloten kern: 15 op 10 s, 21 op 12 s)
#   5. het spel heeft zijn eigen databestanden gelezen
#   6. geen onverwachte schrijfactie naar de NAS
#
# GEBRUIK
#   bash b0-vijfmin.sh                                   # onze huidige kern
#   RG_B0_KERN_DLL=/pad/OpenParrot64.dll bash b0-vijfmin.sh   # de nieuwe kern van de laderbouwer
#   RG_B0_NO_SYSTEMD=1 bash b0-vijfmin.sh                # direct, zonder unit (kort debuggen)
set -u
HIER="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

REPO="${RG_REPO:-$HOME/projects/ravengun}"
TP="${RAVENGUN_TP_DIR:-$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64}"
KERN="$TP/TeknoParrot/TeknoParrot64.dll"
KERN_ORIG_SHA_EXPECTED="f5c7532d6e63848564a4b9233d30706793066d94d3bbafafe45bf72c2b1d6e28"
NIEUW="${RG_B0_KERN_DLL:-$HOME/.local/share/ravengun/openparrot/current/OpenParrotx64/OpenParrot64.dll}"
PFX="${RG_WINEPREFIX:-$HOME/.local/share/ravengun/prefixes/shared/wine}"
WINEBIN="${RG_WINE_BIN:-$HOME/.local/share/ravengun/wine/proton-11.0-2/bin}"
NAS="${RG_NAS_GAME:-}"
[ -n "$NAS" ] || NAS="/home/deck/mnt/spidernas_emulation/Teknoparrot/Luigi's Mansion Arcade"
STOP="${RG_B0_STOP:-300}"        # stopgrens in s game-tijd
HARD="${RG_B0_HARD:-480}"        # harde limiet in s muurtijd
SUDOPW="${RG_SUDO_PW:-deck}"
UIT="${RG_B0_UIT:-/tmp/b0-vijfmin-$(date +%Y%m%d-%H%M%S)}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

zeg()  { printf '\033[1m==\033[0m %s\n' "$*"; }
fout() { printf '\033[31mFOUT\033[0m %s\n' "$*" >&2; }

# ---------------------------------------------------------------- 0. systeem-unit
if [[ -z "${INVOCATION_ID:-}" && "${RG_B0_NO_SYSTEMD:-0}" != 1 ]] && command -v systemd-run >/dev/null 2>&1; then
  UNIT="ravengun-b0-vijfmin-$(date +%H%M%S)"
  echo "Ik zet mezelf in de systemd-user-unit $UNIT -- overleeft een ssh-sessie."
  echo "Volgen:  journalctl --user -u $UNIT -f"
  echo "Uitvoer: $UIT"
  exec systemd-run --user --unit="$UNIT" --collect --same-dir \
       --setenv=RG_B0_UIT="$UIT" --setenv=RG_B0_KERN_DLL="$NIEUW" \
       --setenv=RG_B0_STOP="$STOP" --setenv=RG_B0_HARD="$HARD" \
       --setenv=RG_B0_IN_UNIT=1 --setenv=RG_WINEPREFIX="$PFX" \
       /bin/bash "$0" "$@"
fi

zeg "bench-controle"
EIGEN=""; _p=$$
while [[ "${_p:-0}" -gt 1 ]]; do EIGEN+="$_p "; _p=$(awk '{print $4}' "/proc/$_p/stat" 2>/dev/null); [[ -z "${_p:-}" ]] && break; done
AAN=""
for pat in 'Game.exe' 'VACUUM.exe' 'OpenParrotLoade' 'BudgieLoader.ex' 'BudgieLoader_x6' \
           'TeknoParrotUi' 'TeknoMacaw.exe' 'TeknoMacaw64.ex' 'gamescope' 'gamescope-wl' \
           'gamescopereaper' 'wineserver' 'rg-jvsbridge.ex' 'pipehelper.exe'; do
  for HIT in $(pgrep -x "$pat" 2>/dev/null || true); do
    [[ " $EIGEN " == *" $HIT "* ]] && continue
    AAN+="  comm $pat -> $HIT"$'\n'
  done
done
[[ -n "$AAN" ]] && { fout "de bench is NIET vrij."; printf '%s' "$AAN"; exit 3; }
zeg "bench vrij"
[[ -f "$NIEUW" ]] || { fout "kern-DLL niet gevonden: $NIEUW"; exit 1; }
mkdir -p "$UIT"

ORIG_SHA=$(sha256sum "$KERN" | awk '{print $1}')
NIEUW_SHA=$(sha256sum "$NIEUW" | awk '{print $1}')
BEWAARD="$KERN.b0-bewaard"
herstel_kern() {
  if [[ -f "$BEWAARD" ]]; then rm -f "$KERN"; mv -f "$BEWAARD" "$KERN"; fi
  local nu; nu=$(sha256sum "$KERN" | awk '{print $1}')
  if [[ "$nu" != "$KERN_ORIG_SHA_EXPECTED" ]]; then
    fout "runtime-kern is NIET terug op het origineel: $nu -- verwacht $KERN_ORIG_SHA_EXPECTED"
  else
    echo "[b0] runtime-kern terug op $nu"
  fi
}
GSPID=""
opruimen() {
  herstel_kern
  if [[ -n "$GSPID" ]]; then kill -9 -"$GSPID" 2>/dev/null; sleep 0.5; pkill -9 -f 'gamescope --backend headless' 2>/dev/null; fi
}
trap opruimen EXIT

# ---------------------------------------------------------------- 1. NAS-schrijfcontrole VOOR
nas_snapshot() {
  [[ -d "$NAS" ]] || { echo "geen NAS-map"; return; }
  find "$NAS" -type f -newermt "-1 day" -printf '%T@ %s %p\n' 2>/dev/null | sort | head -50
  echo "--- hashes van de bekende schrijfdoelen ---"
  for f in "$NAS/exe/x64/teknoparrot.ini" "$NAS/Log/play_log.csv" "$NAS/Log/play_number.dat" "$NAS/Ram/CreditConfig.bin"; do
    [[ -e "$f" ]] && sha256sum "$f"
  done
}
nas_snapshot > "$UIT/nas-voor.txt"

# ---------------------------------------------------------------- 2. headless display
zeg "headless gamescope starten"
rm -f "$UIT/gamescope.log"
setsid gamescope --backend headless -W 1920 -H 1080 -- sleep 3600 >"$UIT/gamescope.log" 2>&1 </dev/null &
GSPID=$!
DISP=""
for _ in $(seq 1 40); do DISP=$(grep -o 'Starting Xwayland on :[0-9]*' "$UIT/gamescope.log" 2>/dev/null | tail -1 | grep -o '[0-9]*$'); [[ -n "$DISP" ]] && break; sleep 1; done
[[ -n "$DISP" ]] || { fout "gamescope kwam niet op"; exit 1; }
export DISPLAY=":$DISP" WAYLAND_DISPLAY=gamescope-0
zeg "DISPLAY=:$DISP"

# ---------------------------------------------------------------- 3. kern verwisselen
if [[ "$NIEUW_SHA" != "$ORIG_SHA" ]]; then
  cp -f "$KERN" "$BEWAARD"
  cp -f "$NIEUW" "$KERN"
  [[ "$(sha256sum "$KERN" | awk '{print $1}')" == "$NIEUW_SHA" ]] || { fout "verwisselen mislukt"; herstel_kern; exit 1; }
fi
{
  echo "datum            $(date -Is)"
  echo "route            launch-direct.py LuigisMansion --run, headless gamescope"
  echo "kern-DLL         $NIEUW"
  echo "kern sha256      $NIEUW_SHA"
  echo "runtime VOOR     $ORIG_SHA"
  echo "stopgrens        ${STOP}s game-tijd;  harde limiet ${HARD}s"
  echo "DISPLAY          $DISPLAY"
  echo "WINEDEBUG        -all   GEEN +file -- die trace vertraagt het proces en verandert de levensduur"
  echo "eigen-bestand    gemeten via /proc/<pid>/fd naar de Data-mappen, niet via de trace"
} > "$UIT/opzet.txt"
cat "$UIT/opzet.txt"

# ---------------------------------------------------------------- 3b. DROP-IN (docs/dropins.md)
# Een game-specifieke patch hoort in patches/<Game>/dropin/ met een dropin.ini, NIET in de lader.
# De launcher zet die bestanden in de view en zet de Env-sleutels in de omgeving van de game.
# Zonder controle dat de drop-in echt geladen is, is een meting niets waard.
DROPIN_DIR="${RG_B0_DROPIN:-$HOME/projects/ravengun/patches/LuigisMansion/dropin}"
PLAN_JSON="$UIT/plan.json"
python3 "$REPO/scripts/launch-direct.py" LuigisMansion --plan --json > "$PLAN_JSON" 2>"$UIT/plan.err" || true
DROPIN_AANTAL=$(python3 - "$PLAN_JSON" <<'PY'
import json, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception:
    print(-1); raise SystemExit
print(len(((d.get("dropins") or {}).get("files") or [])))
PY
)
zeg "drop-in: map $DROPIN_DIR -- het plan noemt $DROPIN_AANTAL bestand(en)"
if [[ -d "$DROPIN_DIR" ]]; then
  [[ -f "$DROPIN_DIR/dropin.ini" ]] || fout "drop-in-map zonder dropin.ini: elk bestand heeft een eigen sectie nodig"
  if [[ "$DROPIN_AANTAL" == "0" || "$DROPIN_AANTAL" == "-1" ]]; then
    fout "je gaf een drop-in op maar het plan kent er 0: dan wordt er niets gekopieerd."
    fout "Controleer de sectienamen in dropin.ini (de sectienaam is de BESTANDSNAAM, met pad)."
    exit 1
  fi
  python3 - "$PLAN_JSON" <<'PY' | sed 's/^/  /'
import json, sys
d = json.load(open(sys.argv[1]))
dp = d.get("dropins") or {}
for f in (dp.get("files") or []):
    print("drop-in bestand: %s -> %s" % (f.get("src"), f.get("dest")))
for k, v in (dp.get("env") or {}).items():
    print("drop-in env   : %s=%s" % (k, v))
PY
  # de basenamen voor de maps-controle in het proces
  DROPIN_MAPS=$(python3 - "$PLAN_JSON" <<'PY'
import json, os, sys
d = json.load(open(sys.argv[1]))
namen = [os.path.basename(f.get("dest", "")) for f in ((d.get("dropins") or {}).get("files") or [])]
print(":".join([n for n in namen if n]))
PY
)
  export RG_B0_DROPIN_MAPS="$DROPIN_MAPS"
  # De werkmap van het spel (= de drop-in-basis) en het logboek dat de drop-in ZELF schrijft.
  # Dat logboek is het directe bewijs dat de DLL GELADEN is; zonder logboek is hij niet geladen
  # -- en dat is iets anders dan "hij vond niets", want de hook schrijft altijd eerst een PE-regel.
  DROPIN_BASE=$(python3 - "$PLAN_JSON" <<'PYX'
import json, sys
try:
    print((json.load(open(sys.argv[1])).get("dropins") or {}).get("base") or "")
except Exception:
    print("")
PYX
)
  HOOKLOG_NAME=$(python3 - "$PLAN_JSON" <<'PYX'
import json, sys
try:
    print((json.load(open(sys.argv[1])).get("dropins") or {}).get("env", {}).get("RG_HOOK_LOG", ""))
except Exception:
    print("")
PYX
)
  HOOKLOG="${HOOKLOG_NAME:+$DROPIN_BASE/$HOOKLOG_NAME}"
  export HOOKLOG DROPIN_BASE
  zeg "drop-in in het proces verwacht: ${DROPIN_MAPS:-<geen namen>}"
else
  [[ "${RG_B0_DROPIN:-}" != "" ]] && { fout "opgegeven drop-in-map bestaat niet: $DROPIN_DIR"; exit 1; }
  zeg "geen drop-in voor deze game (map bestaat niet) -- dat is de huidige stand"
  export RG_B0_DROPIN_MAPS=""
fi

# ---------------------------------------------------------------- 4. de run + de bemonstering
zeg "run starten en bemonsteren, elke 30 s game-tijd"
set -m
( cd "$REPO" && env DISPLAY="${DISPLAY:-}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}" \
    XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" WINEDEBUG="-all" \
    timeout "$HARD" python3 scripts/launch-direct.py LuigisMansion --run ) >"$UIT/run.log" 2>&1 &
PGID=$!
set +m

echo "$SUDOPW" | sudo -S env RG_B0_DROPIN_MAPS="${RG_B0_DROPIN_MAPS:-}" python3 "$HIER/b0-vijfmin-sampler.py" "$UIT" "vijfmin" "$STOP" "$HARD" 2>&1 | sed 's/^/  /'

kill -9 -"$PGID" 2>/dev/null; wait "$PGID" 2>/dev/null
RC=$?
# resten wegvegen
for p in $(pgrep -x VACUUM.exe 2>/dev/null); do
  c=$(tr '\0' ' ' < "/proc/$p/cmdline" 2>/dev/null); [[ "$c" == *"LuigisMansion"* ]] && kill -9 "$p" 2>/dev/null
done
pkill -9 -f "OpenParrotLoader64.exe.*LuigisMansion" 2>/dev/null
pkill -9 -f 'rg-jvsbridge' 2>/dev/null; pkill -9 -f 'pipehelper.exe' 2>/dev/null
sleep 1
WINEPREFIX="$PFX" "$WINEBIN/wineserver" -k 2>/dev/null
herstel_kern

# ---------------------------------------------------------------- 5. NAS-schrijfcontrole NA
nas_snapshot > "$UIT/nas-na.txt"
diff <(grep -v '^--- ' "$UIT/nas-voor.txt") <(grep -v '^--- ' "$UIT/nas-na.txt") > "$UIT/nas-verschil.txt" 2>&1 || true

# ---------------------------------------------------------------- 6. oordeel
WERKELIJK=$(grep -a -oE 'kern \(runtime\): .* sha256 [0-9a-f]+' "$UIT/run.log" | head -1 | sed 's/.*sha256 //')
zeg "oordeel"
{
  echo "kern volgens launch-direct: ${WERKELIJK:-niet gevonden}"
  echo "kern die wij bedoelden    : $NIEUW_SHA"
  echo
  cat "$UIT/vijfmin-vijfmin.txt" 2>/dev/null
  echo
  echo "--- NAS-verschil over het venster ---"
  if [ -s "$UIT/nas-verschil.txt" ]; then cat "$UIT/nas-verschil.txt"; else echo "  geen verschil in de bekende schrijfdoelen"; fi
  echo
  echo "--- DROP-IN ---"
  if [[ "${RG_B0_DROPIN_MAPS:-}" == "" && ! -d "$DROPIN_DIR" ]]; then
    echo "  geen drop-in opgegeven voor deze run"
  else
    echo "  drop-in in het proces verwacht: ${RG_B0_DROPIN_MAPS:-<geen>}"
    echo "  regels uit het log van launch-direct:"
    grep -ai 'drop-in\|dropin' "$UIT/run.log" | head -10 | sed 's/^/    /'
    echo "  controles:"
    python3 - "$PLAN_JSON" <<'PY' | sed 's/^/    /'
import hashlib, json, os, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception as e:
    print("plan niet leesbaar: %s" % e); raise SystemExit
for f in ((d.get("dropins") or {}).get("files") or []):
    src, dest = f.get("src"), f.get("dest")
    if not (src and dest):
        continue
    if not os.path.exists(dest):
        print("ONTBREEKT in de view: %s" % dest); continue
    hs = hashlib.sha256(open(src, "rb").read()).hexdigest()
    hd = hashlib.sha256(open(dest, "rb").read()).hexdigest()
    print("%s  %s  bron==doel: %s" % ("OK " if hs == hd else "WIJKT AF", os.path.basename(dest), hs == hd))
PY
    echo "  (de maps-controle per monster staat in vijfmin-vijfmin.txt als 'drop-in in proces')"
    echo
    echo "  --- HET LOGBOEK VAN DE DROP-IN ZELF ---"
    if [ -n "${HOOKLOG:-}" ] && [ -f "$HOOKLOG" ]; then
      echo "  AANWEZIG: $HOOKLOG ($(stat -c%s "$HOOKLOG") B)"
      echo "  eerste regels (de PE-regel met ImageBase en SizeOfImage hoort er altijd te staan):"
      head -6 "$HOOKLOG" | sed 's/^/    /'
      echo "  samenvatting:"
      grep -aiE 'totaal|aangehaakt|afgekeurd|TODO' "$HOOKLOG" | tail -5 | sed 's/^/    /'
    else
      echo "  GEEN LOGBOEK (${HOOKLOG:-geen pad bekend}) -- dan is de DLL NIET GELADEN."
      echo "  Dat is iets anders dan 'hij vond niets': de hook schrijft altijd eerst een PE-regel."
    fi
  fi
  echo
  echo "--- SUCCESCRITERIA: JA/NEE met de gemeten waarde ---"
  LAATSTE=$(grep -a '^t=' "$UIT/vijfmin-vijfmin.txt" 2>/dev/null | tail -1)
  GHAALD=$(grep -ac 'STOPGRENS' "$UIT/vijfmin-vijfmin.txt" 2>/dev/null)
  EP=$(echo "$LAATSTE" | grep -c 'TERUGGEZET')
  TXT=$(echo "$LAATSTE" | grep -oE '\.text +[0-9.]+% nul')
  THR=$(echo "$LAATSTE" | grep -oE 'threads +[0-9]+' | grep -oE '[0-9]+')
  MAXD=$(grep -a 'MAX drop-in in proces' "$UIT/vijfmin-vijfmin.txt" 2>/dev/null | tail -1)
  echo "1. 300 s game-tijd gehaald        : $([ "${GHAALD:-0}" -gt 0 ] && echo JA || echo NEE)"
  echo "     laatste levenslijn: $LAATSTE"
  echo "2. entry point teruggezet (20 B)  : $([ "${EP:-0}" -gt 0 ] && echo JA || echo NEE)"
  echo "3. .text gevuld                   : $([ -n "$TXT" ] && echo JA || echo NEE)   ${TXT:-<geen meting>}"
  echo "4. threads ruim boven 1           : $([ -n "$THR" ] && [ "${THR:-0}" -gt 1 ] && echo JA || echo NEE)   threads ${THR:-?}"
  echo "5. eigen databestanden            : NIET GEMETEN met dit instrument."
  echo "     Het spel opent, leest en SLUIT zijn bestanden meteen; op een cadans van 30 s vangt"
  echo "     /proc/<pid>/fd er bijna nooit een. Dat is een VALSE NEGATIEF, geen nul."
  echo "     Wordt apart gemeten met de +file-trace: bash b0-run.sh a   (die trace vertraagt het"
  echo "     proces en verandert de levensduur, daarom staat hij los van deze proef)."
  echo "6. geen onverwachte NAS-schrijfactie: $([ -s "$UIT/nas-verschil.txt" ] && echo CONTROLEREN || echo JA)   zie nas-verschil.txt"
  echo "7. drop-in in het plan            : ${DROPIN_AANTAL:-?} bestand(en)"
  echo "8. drop-in in de view (sha256)    : zie de DROP-IN-sectie hierboven"
  echo "9. drop-in in het PROCES          : ${MAXD:-niet gemeten}"
  if [ -n "${RG_B0_DROPIN_MAPS:-}" ]; then
    PAAR=$(echo "${MAXD:-}" | grep -oE '[0-9]+/[0-9]+' | tail -1)
    MN=$(echo "$PAAR" | cut -d/ -f1); MT=$(echo "$PAAR" | cut -d/ -f2)
    if [ -n "${MN:-}" ] && [ -n "${MT:-}" ] && [ "${MT:-0}" -gt 0 ] && [ "${MN:-0}" -lt "${MT:-0}" ]; then
      echo
      echo "!! DEZE RUN IS ONGELDIG: de drop-in is NIET geladen (${MN} van ${MT} in het proces)."
      echo "   Ook het logboek van de drop-in zelf ontbreekt dan; zie de DROP-IN-sectie hierboven."
      echo "   Ook als het spel leeft zegt dat niets over de drop-in: het betekent dan alleen dat de"
      echo "   drop-in voor deze game NIET NODIG was. Dat is een eigen uitkomst, geen succes."
    fi
  fi
} | tee "$UIT/oordeel.txt"

zeg "alles staat in $UIT"
