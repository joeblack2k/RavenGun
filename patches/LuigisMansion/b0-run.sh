#!/usr/bin/env bash
# B0: de beslissende bisectie voor Luigi's Mansion Arcade.
#
# VRAAG
#   Het spel sterft met exitcode 32 op ONZE kern en op een LEGE DLL, terwijl de GESLOTEN kern
#   leeft. Is dat een aanhechtingsprobleem, of is 32 de eigen reactie van het spel op zijn
#   omgeving -- en levert de gesloten kern dus iets dat het spel nodig heeft?
#
# OPZET -- via de WERKENDE route, niet via een eigen wine-aanroep
#   De eerste opzet startte wine zelf met de verkeerde prefix, de verkeerde werkmap van de
#   lader en zonder de hulpketen. Toen stierf zelfs de GESLOTEN kern (exitcode 5). Daarom laat
#   dit script de bestaande route het werk doen:
#
#       python3 scripts/launch-direct.py LuigisMansion --run
#
#   Die zet prefix, werkmap van de lader, hulpketen (rg-jvsbridge + TP_LAUNCH_SESSION_ID) en
#   display goed. DE ENIGE VARIABELE is de KERN-DLL: launch-direct geeft de kern als argv[1]
#   van de lader mee en de lader doet daar `+ ".dll"` achter. Wij verwisselen tijdelijk de
#   inhoud van die ENE DLL, en zetten hem daarna terug.
#
#   (0) ZONDER kern      gewoon `wine VACUUM.exe`, geen lader, geen enkele injectie
#   (a) origineel        de controle die MOET leven
#   (b) OpenParrot64     onze kern
#   (c) leeg.dll         DllMain die TRUE teruggeeft, geen haak, geen thread, geen import
#   (d) leeg0.dll        geen entry point en geen imports: alleen de module wordt gemapt
#
# VOORAF OPGESCHREVEN UITKOMSTEN
#   (0) geeft 32  -> 32 is de EIGEN reactie van het spel; "onze kern -> 32" en "lege DLL -> 32"
#                    zijn dan GEEN bewijs over aanhechting. De vraag wordt dan: wat levert de
#                    gesloten kern dat het spel laat leven?
#   (0) leeft     -> dan is de aanhechting wel de variabele, en is (c) de beslissende run.
#   (a) sterft    -> de OPSTELLING is fout; (0)/(b)/(c)/(d) zijn niet te interpreteren. STOP.
#   (c) LEEFT     -> de aanhechting zelf is het probleem niet; onze kern doet iets dat het spel
#                    merkt. Route: bisectieladder B1 t/m B3, daarna een minimale per-game hook.
#   (c) STERFT met 32 terwijl (a) leeft -> ELKE in-process aanhechting struikelt over iets.
#                    STOP: eigenaarsbeslissing, geen technisch obstakel. Niet omheen werken.
#
# LES UIT DE EERSTE HERMETING (staat ook in B0-LEGE-DLL.md)
#   Een `herstel()` met een vlag die maar een keer waar is, zet de kern daarna nooit meer
#   terug: de derde run draaide nog met de DLL van de tweede. Daarom is herstel hier
#   idempotent op het BESTAAN van de backup, en controleert elke run achteraf de sha256 die
#   launch-direct ZELF in zijn logboek schrijft tegen de DLL die wij bedoelden. Een run waar
#   dat niet klopt wordt als ONGELDIG gemarkeerd.
#
# VEILIGHEID
#   - Weigert te starten als er iets van een game, lader, wineserver of gamescope draait.
#   - Alleen-lezen op de NAS. Raakt geen prefix-instellingen aan.
#   - Zet de kern-DLL altijd terug (trap) en controleert dat met sha256.
#   - Veegt na elke run resten weg; een orphan vergiftigt de volgende meting.
#
# GEBRUIK
#   bash b0-run.sh              # 0 a b c d + de twee WINEDEBUG-runs
#   bash b0-run.sh a            # alleen de controle -- doe dit EERST
#   RG_B0_MAX=45 bash b0-run.sh 0 b c d debug
set -u
HIER="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

REPO="${RG_REPO:-$HOME/projects/ravengun}"
TP="${RAVENGUN_TP_DIR:-$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64}"
OPC="$HOME/.local/share/ravengun/openparrot/current/OpenParrotx64"
B0="${B0_DIR:-$HOME/.local/share/ravengun/b0}"
KERN="$TP/TeknoParrot/TeknoParrot64.dll"
PFX="${RG_WINEPREFIX:-$HOME/.local/share/ravengun/prefixes/shared/wine}"
WINE="${RG_WINE_BIN:-$HOME/.local/share/ravengun/wine/proton-11.0-2/bin}/wine"
VIEW="${RG_VIEW:-$HOME/.local/share/ravengun/runtime/views/LuigisMansion}"
WERKMAP="$VIEW/exe/x64"
GST="${RG_GST:-$HOME/.local/share/ravengun/tools/gst-extra/plugins}"
MAX="${RG_B0_MAX:-45}"
MONSTER="${RG_B0_MONSTER:-0.25}"
UIT="${RG_B0_UIT:-/tmp/b0-luigi-$(date +%Y%m%d-%H%M%S)}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

zeg()  { printf '\033[1m==\033[0m %s\n' "$*"; }
let_op(){ printf '\033[33m!!\033[0m %s\n' "$*"; }
fout() { printf '\033[31mFOUT\033[0m %s\n' "$*" >&2; }

# ---------------------------------------------------------------- 0. veiligheid
# LET OP: `pgrep -f` matcht de HELE commandoregel, en die van ONZELF (of van de ssh-sessie
# waarin dit script loopt) bevat het pad .../runtime/TeknoParrotUi-2.0.0.130-linux-x64/... .
# Zonder uitsluiting weigert het script dus op zijn eigen aanroep -- dat is precies wat er
# gebeurde. Daarom eerst onze eigen pid en al zijn voorouders, en die overslaan.
EIGEN=""
_p=$$
while [[ "${_p:-0}" -gt 1 ]]; do
  EIGEN+="$_p "
  _p=$(awk '{print $4}' "/proc/$_p/stat" 2>/dev/null)
  [[ -z "${_p:-}" ]] && break
done
AAN=""
# Exacte procesnamen (comm, door Linux op 15 tekens afgekapt). GEEN -f: dat matcht de
# hele commandoregel en dus ook elk proces dat de naam alleen noemt.
for pat in 'Game.exe' 'VACUUM.exe' 'OpenParrotLoade' 'BudgieLoader.ex' 'BudgieLoader_x6' \
           'TeknoParrotUi' 'TeknoMacaw.exe' 'TeknoMacaw64.ex' 'gamescope' 'gamescope-wl' \
           'gamescopereaper' 'wineserver' 'rg-jvsbridge.ex' 'pipehelper.exe'; do
  for HIT in $(pgrep -x "$pat" 2>/dev/null || true); do
    [[ " $EIGEN " == *" $HIT "* ]] && continue
    AAN+="  comm $pat -> $HIT  $(tr '\0' ' ' < "/proc/$HIT/cmdline" 2>/dev/null | cut -c1-90)"$'\n'
  done
done
if [[ -n "$AAN" ]]; then fout "de bench is NIET vrij; deze bisectie start niets."; printf '%s' "$AAN"; exit 3; fi
zeg "bench vrij (eigen procesboom $EIGEN uitgesloten)"
[[ -f "$KERN" ]] || { fout "gesloten kern niet gevonden: $KERN"; exit 1; }
[[ -f "$OPC/OpenParrot64.dll" ]] || { fout "onze kern niet gevonden"; exit 1; }
[[ -f "$B0/leeg.dll" && -f "$B0/leeg0.dll" ]] || { fout "B0-DLL's ontbreken: draai b0-bouw.sh"; exit 1; }
mkdir -p "$UIT"

ORIG_SHA=$(sha256sum "$KERN" | awk '{print $1}')
BEWAARD="$KERN.b0-bewaard"
herstel() {   # idempotent: herstelt zolang de backup bestaat, controleert daarna altijd
  if [[ -f "$BEWAARD" ]]; then rm -f "$KERN"; mv -f "$BEWAARD" "$KERN"; fi
  local nu; nu=$(sha256sum "$KERN" | awk '{print $1}')
  if [[ "$nu" != "$ORIG_SHA" ]]; then
    fout "kern-DLL wijkt af: $nu (verwacht $ORIG_SHA) -- zet hem met de hand terug uit $BEWAARD"
  fi
}
opruimen() { herstel; if [[ -n "${GSPID:-}" ]]; then kill -9 -"$GSPID" 2>/dev/null; sleep 0.5; pkill -9 -f 'gamescope --backend headless' 2>/dev/null; fi; }
trap opruimen EXIT

# ---------------------------------------------------------------- 1. headless display
if [[ "${RG_B0_NO_GAMESCOPE:-0}" != 1 ]]; then
  zeg "headless gamescope starten, het scherm van de eigenaar blijft vrij"
  rm -f "$UIT/gamescope.log"
  setsid gamescope --backend headless -W 1920 -H 1080 -- sleep 3600 >"$UIT/gamescope.log" 2>&1 </dev/null &
  GSPID=$!
  DISP=""
  for _ in $(seq 1 40); do
    DISP=$(grep -o 'Starting Xwayland on :[0-9]*' "$UIT/gamescope.log" 2>/dev/null | tail -1 | grep -o '[0-9]*$')
    [[ -n "$DISP" ]] && break; sleep 1
  done
  [[ -n "$DISP" ]] || { fout "gamescope kwam niet op"; tail -20 "$UIT/gamescope.log"; exit 1; }
  export DISPLAY=":$DISP" WAYLAND_DISPLAY=gamescope-0
  zeg "DISPLAY=:$DISP WAYLAND_DISPLAY=gamescope-0"
fi

{
  echo "datum           $(date -Is)"
  echo "route           python3 $REPO/scripts/launch-direct.py LuigisMansion --run"
  echo "run 0           cd $WERKMAP && wine VACUUM.exe   (geen lader, geen kern)"
  echo "kern-DLL-doel   $KERN"
  echo "kern sha256 VOOR $ORIG_SHA"
  echo "onze kern       $(sha256sum "$OPC/OpenParrot64.dll")"
  echo "leeg.dll        $(sha256sum "$B0/leeg.dll")"
  echo "leeg0.dll       $(sha256sum "$B0/leeg0.dll")"
  echo "DISPLAY         ${DISPLAY:-<geen>}  WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-<geen>}"
  echo "tijdslimiet     ${MAX}s per run"
} > "$UIT/opzet.txt"
cat "$UIT/opzet.txt"

# ---------------------------------------------------------------- 2. hulpjes
game_pid() {
  local p c
  for p in $(pgrep -x VACUUM.exe 2>/dev/null); do
    c=$(tr '\0' ' ' < "/proc/$p/cmdline" 2>/dev/null)
    [[ "$c" == *"LuigisMansion"* ]] && { echo "$p"; return; }
  done
}
threads_van() { local p="$1"; [[ -d "/proc/$p" ]] && ls "/proc/$p/task" 2>/dev/null | wc -l || echo "weg"; }
# IJKPUNT: op de ruwe trace van (a) geeft dit 3736 CreateFileW-regels, 1089 unieke paden en
# 393 eigen Data/DataGlobal-paden. Een teller die 0 zegt moet dat eerst laten zien.
bestandstelling() {
  local f="$1"
  local totaal uniek eigen
  totaal=$(grep -ac 'CreateFileW' "$f" 2>/dev/null); totaal=${totaal:-0}
  uniek=$(grep -aoE 'CreateFileW L"[^"]*"' "$f" 2>/dev/null | sed 's/^CreateFileW L"//; s/"$//' | sort -u | wc -l)
  eigen=$(grep -aoE 'CreateFileW L"[^"]*"' "$f" 2>/dev/null | sed 's/^CreateFileW L"//; s/"$//' | sort -u | grep -ciE 'Data(Global)?[\\/]')
  echo "CreateFileW-regels: ${totaal}   unieke paden: ${uniek}   eigen Data/DataGlobal-paden: ${eigen}"
  echo "OORDEEL-BESTANDEN: $([[ "${eigen:-0}" -gt 0 ]] && echo 'opent zijn eigen bestanden' || echo 'GEEN eigen bestand gezien')"
}
veeg() {
  local p c
  for p in $(pgrep -x VACUUM.exe 2>/dev/null); do
    c=$(tr '\0' ' ' < "/proc/$p/cmdline" 2>/dev/null)
    [[ "$c" == *"LuigisMansion"* ]] && { let_op "rest weggeveegd: VACUUM.exe $p"; kill -9 "$p" 2>/dev/null; }
  done
  pkill -9 -f "OpenParrotLoader64.exe.*LuigisMansion" 2>/dev/null
  pkill -9 -f 'rg-jvsbridge' 2>/dev/null; pkill -9 -f 'pipehelper.exe' 2>/dev/null
  sleep 1
  WINEPREFIX="$PFX" "${WINE%/wine}/wineserver" -k 2>/dev/null
  sleep 1
}

# ---------------------------------------------------------------- 3. de meetlus
# $1 label, $2 pgid-variabele, $3 = logbestand  -> zet TH_*, eerste_th, max_th, t, gpid_eerste, laatste
meet() {
  local pgid="$1" log="$2"
  local t=0 gp th
  gpid_eerste=""; laatste=""; eerste_th="-"; max_th=0; t_eind=0
  unset TH; declare -gA TH
  while true; do
    gp=$(game_pid)
    if [[ -n "$gp" ]]; then
      laatste="$gp"; th=$(threads_van "$gp")
      [[ -z "$gpid_eerste" ]] && { gpid_eerste="$gp"; eerste_th="$th"; }
      [[ "$th" =~ ^[0-9]+$ ]] && (( th > max_th )) && max_th=$th
      for m in 0 2 5 10 20; do
        [[ -n "${TH[$m]:-}" ]] && continue
        (( $(printf '%.0f' "$t") >= m )) && TH[$m]="$th"
      done
    fi
    kill -0 "$pgid" 2>/dev/null || break
    sleep "$MONSTER"
    t=$(awk -v a="$t" -v b="$MONSTER" 'BEGIN{print a+b}')
  done
  t_eind=$(printf '%.1f' "$t")
}

# ---------------------------------------------------------------- 4. een run via launch-direct
doe_run() {
  local label="$1" variant="$2" winedebug="$3"
  local out="$UIT/$label" rc ld_sha bedoeld_sha geldig="ja"

  bedoeld_sha=""; [[ -n "$variant" ]] && bedoeld_sha=$(sha256sum "$variant" | awk '{print $1}')

  if [[ -n "$variant" ]]; then
    [[ -f "$BEWAARD" ]] || cp -f "$KERN" "$BEWAARD"
    cp -f "$variant" "$KERN"
    local nu; nu=$(sha256sum "$KERN" | awk '{print $1}')
    [[ "$nu" == "$bedoeld_sha" ]] || { fout "verwisselen mislukt ($nu != $bedoeld_sha)"; herstel; return 1; }
  fi

  {
    echo "run            $label"
    echo "kern-DLL       ${variant:-$KERN (origineel, ongemoeid)}"
    echo "bedoelde sha256 $([[ -n "$bedoeld_sha" ]] && echo "$bedoeld_sha" || echo "$ORIG_SHA")"
    echo "WINEDEBUG      $winedebug"
    echo "commandoregel  cd $REPO && WINEDEBUG=$winedebug DISPLAY=${DISPLAY:-} WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-} \\"
    echo "                 timeout $MAX python3 scripts/launch-direct.py LuigisMansion --run"
  } > "$out.cmdline"

  zeg "run $label  kern=${variant:-origineel}  WINEDEBUG=$winedebug"
  sed 's/^/    /' "$out.cmdline"

  set -m
  ( cd "$REPO" && env DISPLAY="${DISPLAY:-}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}" \
      XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" WINEDEBUG="$winedebug" \
      timeout "$MAX" python3 scripts/launch-direct.py LuigisMansion --run ) >"$out.log" 2>&1 &
  local pgid=$!
  set +m
  meet "$pgid" "$out.log"
  wait "$pgid" 2>/dev/null; rc=$?

  # de kern meteen terugzetten, VOOR het opruimen
  [[ -n "$variant" ]] && herstel
  veeg

  # CONTROLE: welke sha256 heeft launch-direct ZELF gebruikt?
  ld_sha=$(grep -a -oE 'kern \(runtime\): .* sha256 [0-9a-f]+' "$out.log" 2>/dev/null | head -1 | sed 's/.*sha256 //')
  local verwacht="${bedoeld_sha:-$ORIG_SHA}"
  [[ -n "$ld_sha" && "$ld_sha" != "$verwacht" ]] && { geldig="NEE"; let_op "run $label is ONGELDIG: launch-direct gebruikte $ld_sha, bedoeld was $verwacht"; }
  [[ -z "$ld_sha" ]] && geldig="onbekend"

  {
    echo "kern           ${variant:-origineel}"
    echo "bedoelde sha256 $verwacht"
    echo "werkelijk gebruikt (uit het log van launch-direct) ${ld_sha:-<niet gevonden>}"
    echo "GELDIG         $geldig"
    echo "exitcode       timeout/launch-direct = $rc   $(grep -aoE 'klaar \(exitcode [^)]*\)' "$out.log" 2>/dev/null | tail -1)"
    echo "looptijd       ${t_eind} s (limiet ${MAX}s)"
    echo "spelproces     ${gpid_eerste:-nooit gezien}  laatst gezien ${laatste:--}"
    echo "threads        bij eerste verschijning: $eerste_th   maximum: $max_th"
    echo "               vast grid op run-tijd  t=0:${TH[0]:-n/a}  t=2:${TH[2]:-n/a}  t=5:${TH[5]:-n/a}  t=10:${TH[10]:-n/a}  t=20:${TH[20]:-n/a}"
    echo "bestanden      $(bestandstelling "$out.log" | head -1)"
    echo "               $(bestandstelling "$out.log" | tail -1)"
    echo "laatste eigen paden:"
    grep -aoE 'CreateFileW L"[^"]*"' "$out.log" 2>/dev/null | sed 's/^CreateFileW L"//; s/"$//' | grep -iE 'Data(Global)?[\\/]' | sort -u | head -6 | sed 's/^/    /'
    echo "resterende VACUUM.exe: $(pgrep -x VACUUM.exe 2>/dev/null | wc -l)"
    echo
    echo "--- laatste 12 regels ---"
    tail -12 "$out.log" | cut -c1-200
  } > "$out.verslag"
  sed 's/^/    /' "$out.verslag"
  echo "$rc" > "$out.exit"
  echo "$geldig" > "$out.geldig"
}

# ---------------------------------------------------------------- 5. run 0: ZONDER kern
doe_run0() {
  local label="0-zonder-kern" out="$UIT/0-zonder-kern"
  {
    echo "run            $label  (geen lader, geen kern, geen injectie)"
    echo "commandoregel  cd $WERKMAP && WINEPREFIX=$PFX GST_PLUGIN_PATH=$GST \\"
    echo "                 DISPLAY=${DISPLAY:-} WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-} timeout $MAX $WINE VACUUM.exe"
  } > "$out.cmdline"
  zeg "run $label -- gewoon wine VACUUM.exe"
  sed 's/^/    /' "$out.cmdline"

  set -m
  ( cd "$WERKMAP" && env WINEPREFIX="$PFX" GST_PLUGIN_PATH="$GST" WINEDEBUG="${RG_B0_WINEDEBUG:--all,+file}" \
      DISPLAY="${DISPLAY:-}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}" XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" \
      timeout "$MAX" "$WINE" VACUUM.exe ) >"$out.log" 2>&1 &
  local pgid=$!
  set +m
  meet "$pgid" "$out.log"
  wait "$pgid" 2>/dev/null; local rc=$?
  veeg
  {
    echo "kern           GEEN (geen lader, geen DLL geladen)"
    echo "bedoelde sha256 n.v.t."
    echo "GELDIG         ja"
    echo "exitcode       timeout = $rc"
    echo "looptijd       ${t_eind} s (limiet ${MAX}s)"
    echo "spelproces     ${gpid_eerste:-nooit gezien}  laatst gezien ${laatste:--}"
    echo "threads        bij eerste verschijning: $eerste_th   maximum: $max_th"
    echo "               vast grid op run-tijd  t=0:${TH[0]:-n/a}  t=2:${TH[2]:-n/a}  t=5:${TH[5]:-n/a}  t=10:${TH[10]:-n/a}  t=20:${TH[20]:-n/a}"
    echo "bestanden      $(bestandstelling "$out.log" | head -1)"
    echo "               $(bestandstelling "$out.log" | tail -1)"
    echo "resterende VACUUM.exe: $(pgrep -x VACUUM.exe 2>/dev/null | wc -l)"
    echo
    echo "--- laatste 12 regels ---"
    tail -12 "$out.log" | cut -c1-200
  } > "$out.verslag"
  sed 's/^/    /' "$out.verslag"
  echo "$rc" > "$out.exit"
  echo "ja" > "$out.geldig"
}

# ---------------------------------------------------------------- 6. de runs
WAT=("$@")
[[ ${#WAT[@]} -eq 0 ]] && WAT=(0 a b c d debug)
for r in "${WAT[@]}"; do
  case "$r" in
    0)     doe_run0 ;;
    a)     doe_run "a-origineel"  ""                      "-all,+file" ;;
    b)     doe_run "b-onze-kern"  "$OPC/OpenParrot64.dll"  "${RG_B0_WINEDEBUG:--all,+file}" ;;
    c)     doe_run "c-lege-dll"   "$B0/leeg.dll"           "${RG_B0_WINEDEBUG:--all,+file}" ;;
    d)     doe_run "d-lege-dll-0" "$B0/leeg0.dll"          "${RG_B0_WINEDEBUG:--all,+file}" ;;
    debug)
      doe_run "dbg-origineel" ""                       "${RG_B0_WINEDEBUG_DEBUG:-+thread,+module}"
      doe_run "dbg-onze"      "$OPC/OpenParrot64.dll"  "${RG_B0_WINEDEBUG_DEBUG:-+thread,+module}"
      ;;
    *) fout "onbekende run '$r' (0 a b c d debug)"; exit 2 ;;
  esac
done

# ---------------------------------------------------------------- 7. vergelijking
if [[ -f "$UIT/dbg-origineel.log" && -f "$UIT/dbg-onze.log" ]] \
   && [[ "$(cat "$UIT/dbg-origineel.geldig" 2>/dev/null)" == "ja" ]]; then
  zeg "WINEDEBUG-vergelijking: wat doet de GESLOTEN kern dat ONZE niet doet?"
  for kant in origineel onze; do
    f="$UIT/dbg-$kant.log"
    grep -aoE '^[0-9a-f]{4}:[0-9a-f]{4}:' "$f" 2>/dev/null | sed 's/:$//' | sort -u > "$UIT/dbg-$kant.threads"
    grep -aoE '[A-Za-z0-9_.-]+\.(dll|DLL|exe|EXE)' "$f" 2>/dev/null | tr 'A-Z' 'a-z' | sort -u > "$UIT/dbg-$kant.modulenamen"
    head -400 "$f" > "$UIT/dbg-$kant.kop"
  done
  {
    echo "modules die de GESLOTEN kern heeft en ONZE niet:"
    comm -23 "$UIT/dbg-origineel.modulenamen" "$UIT/dbg-onze.modulenamen" | sed 's/^/  /'
    echo
    echo "modules die ONZE kern heeft en de GESLOTEN kern niet:"
    comm -13 "$UIT/dbg-origineel.modulenamen" "$UIT/dbg-onze.modulenamen" | sed 's/^/  /'
    echo
    echo "threads van de GESLOTEN kern: $(wc -l < "$UIT/dbg-origineel.threads")   van ONZE: $(wc -l < "$UIT/dbg-onze.threads")"
    echo
    echo "raakt de gesloten kern de JVS-pipe of het gedeelde geheugen?"
    grep -aiE 'pipe|JvsState|shm|jvsbridge|helper' "$UIT/dbg-origineel.log" | head -20 | sed 's/^/  /'
    echo
    echo "en ONZE kern:"
    grep -aiE 'pipe|JvsState|shm|jvsbridge|helper' "$UIT/dbg-onze.log" | head -20 | sed 's/^/  /'
    echo
    echo "regelverschil in de eerste 400 regels, gesloten naar onze:"
    diff "$UIT/dbg-origineel.kop" "$UIT/dbg-onze.kop" | head -60
  } > "$UIT/vergelijking.txt"
  sed 's/^/    /' "$UIT/vergelijking.txt"
fi

# ---------------------------------------------------------------- 8. oordeel
zeg "oordeel"
{
  echo "kern sha256 NA alle runs: $(sha256sum "$KERN" | awk '{print $1}')   bij de start: $ORIG_SHA"
  [[ "$(sha256sum "$KERN" | awk '{print $1}')" == "$ORIG_SHA" ]] && echo "  -> gelijk, de runtime is ongemoeid" || echo "  -> WIJKT AF"
  echo
  printf '%-16s %-6s %-6s %s\n' run exit geldig "kern (werkelijk)"
  for r in 0-zonder-kern a-origineel d-lege-dll-0 c-lege-dll b-onze-kern dbg-origineel dbg-onze; do
    [[ -f "$UIT/$r.exit" ]] || continue
    printf '%-16s %-6s %-6s %s\n' "$r" "$(cat "$UIT/$r.exit")" "$(cat "$UIT/$r.geldig" 2>/dev/null)" \
      "$(grep -a -m1 'werkelijk gebruikt' "$UIT/$r.verslag" 2>/dev/null | sed 's/.*) //')"
  done
  echo
  A=$(cat "$UIT/a-origineel.exit" 2>/dev/null || echo "?"); A_G=$(cat "$UIT/a-origineel.geldig" 2>/dev/null || echo "?")
  Z=$(cat "$UIT/0-zonder-kern.exit" 2>/dev/null || echo "?"); C=$(cat "$UIT/c-lege-dll.exit" 2>/dev/null || echo "?")
  A_LEEFT=0; [[ "$A" == "124" ]] && A_LEEFT=1
  Z_LEEFT=0; [[ "$Z" == "124" ]] && Z_LEEFT=1
  C_LEEFT=0; [[ "$C" == "124" ]] && C_LEEFT=1
  if [[ "$A_G" != "ja" ]]; then
    echo "  De CONTROLE (a) is ongeldig: de gebruikte kern was niet de bedoelde. Niets interpreteren."
  elif [[ $A_LEEFT -eq 0 ]]; then
    echo "  (a) leeft NIET -- de OPSTELLING is fout; alle andere runs zijn ongeldig. STOP."
  else
    echo "  (a) LEEFT (exitcode 124 = liep nog toen de limiet om was). De opstelling is goed."
    echo
    if [[ $Z_LEEFT -eq 1 ]]; then
      echo "  (0) ZONDER kern LEEFT ook: dan is het laden van een kern niet de doodsoorzaak, en"
      echo "      is de vraag wat ONZE kern anders doet dan de gesloten kern (zie vergelijking.txt)."
    else
      echo "  (0) ZONDER kern STERFT met $Z: 32 is dan de EIGEN reactie van het spel op zijn"
      echo "      omgeving, en 'onze kern -> 32' is GEEN bewijs over aanhechting. De echte vraag"
      echo "      is dan: WAT levert de gesloten kern dat het spel laat leven? Zie vergelijking.txt."
    fi
    echo
    if [[ $C_LEEFT -eq 1 ]]; then
      echo "  (c) LEGE DLL LEEFT: de aanhechting zelf is het probleem niet."
    else
      echo "  (c) LEGE DLL STERFT met $C: ook zonder enige code van ons gaat het spel eruit."
      echo "      Dat wijst op (0) hierboven, niet op een aanhechtingsprobleem."
    fi
  fi
  echo
  echo "resten: $(pgrep -a -f 'VACUUM.exe|OpenParrotLoader|rg-jvsbridge' 2>/dev/null | wc -l) proces(sen)"
} | tee "$UIT/oordeel.txt"

zeg "alles staat in $UIT"

