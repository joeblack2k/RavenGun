#!/usr/bin/env bash
# luigi-preflight.sh -- een-minuut-controle voor Luigi's Mansion Arcade.
#
# Draai dit VOORDAT je een dure poging doet (de game laadt ~45 s en speelt dan een film).
# Het kijkt naar alles wat in eerdere rondes een run heeft laten mislukken aan de
# OMGEVING, zodat een mislukte start daarna aan de kern toe te schrijven is en niet aan
# een ontbrekende link of plugin.
#
# Alleen lezen. Verandert niets, start niets. Exitcode 0 = groen, 1 = er is iets mis.
#
# Gebruik:  bash luigi-preflight.sh            # de standaardview
#           RG_VIEW=<pad> bash luigi-preflight.sh
#           bash luigi-preflight.sh --kern     # + welke kern er echt gebruikt wordt

set -u
VIEW="${RG_VIEW:-$HOME/.local/share/ravengun/runtime/views/LuigisMansion}"
GST="${RG_GST:-$HOME/.local/share/ravengun/tools/gst-extra/plugins}"
PFX="${RG_WINEPREFIX:-$HOME/.local/share/ravengun/prefixes/shared/wine}"
U="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64"
OPC="$HOME/.local/share/ravengun/openparrot/current"
GAME="/home/deck/mnt/spidernas_emulation/Teknoparrot/Luigi's Mansion Arcade"

MIS=0; WAARSCH=0
ok()   { printf '  \033[32mOK  \033[0m %s\n' "$*"; }
mis()  { printf '  \033[31mMIS \033[0m %s\n' "$*"; MIS=$((MIS+1)); }
wrs()  { printf '  \033[33mLET \033[0m %s\n' "$*"; WAARSCH=$((WAARSCH+1)); }
kop()  { printf '\n\033[1m%s\033[0m\n' "$*"; }

kop "1. De view (de symlink-constructie waar de game in draait)"
[[ -d "$VIEW" ]] || { mis "view bestaat niet: $VIEW (scripts/maak-views.sh?)"; }
if [[ -d "$VIEW" ]]; then
  [[ -L "$VIEW/Data" ]] && [[ -d "$VIEW/Data" ]] \
    && ok "Data -> $(readlink "$VIEW/Data")  (de game vraagt ..\\..\\Data\\...)" \
    || mis "Data ontbreekt of wijst niet: de game stopt dan met een access violation"
  [[ -d "$VIEW/DataGlobal/EN/Data" ]] && ok "DataGlobal/EN/Data aanwezig" \
    || mis "DataGlobal/EN/Data ontbreekt"
  [[ -L "$VIEW/exe" ]] && ok "exe -> $(readlink "$VIEW/exe")" || wrs "exe is geen symlink (mag)"
  for m in System Model Sound; do
    [[ -d "$VIEW/Data/$m" ]] && ok "Data/$m leesbaar" || mis "Data/$m niet leesbaar"
  done
  [[ -d "$VIEW/Data/Movie" ]] && ok "Data/Movie aanwezig ($(ls -1 "$VIEW/Data/Movie" 2>/dev/null | wc -l) films)" \
    || mis "Data/Movie ontbreekt -- de game wacht dan op een zwart scherm"
  [[ -r "$VIEW/Data/Movie/title_movie.wmv" ]] && ok "title_movie.wmv leesbaar" \
    || mis "title_movie.wmv niet leesbaar -- de game sluit af bij de eerste film"
fi

kop "2. De exe en de werkmap"
if [[ -f "$VIEW/exe/x64/VACUUM.exe" ]]; then
  SZ=$(stat -c%s "$VIEW/exe/x64/VACUUM.exe")
  [[ "$SZ" == 3436544 ]] && ok "VACUUM.exe 3.436.544 B (zoals verwacht)" \
    || mis "VACUUM.exe is $SZ B, niet 3.436.544 -- verkeerd bestand of gewijzigd"
else
  mis "VACUUM.exe ontbreekt in $VIEW/exe/x64"
fi
[[ -d "$VIEW/exe/x64" ]] && ok "werkmap $VIEW/exe/x64 bestaat (de game zoekt ..\\..\\DataGlobal\\...)" \
  || mis "werkmap ontbreekt"
[[ -w "$VIEW/exe/x64" ]] && ok "werkmap is beschrijfbaar (de lader schrijft teknoparrot.ini)" \
  || wrs "werkmap is niet beschrijfbaar"
for d in Ram Log; do
  [[ -w "$VIEW/$d" ]] && ok "$d/ beschrijfbaar (de game schrijft daar)" || wrs "$d/ niet beschrijfbaar"
done

kop "3. GStreamer voor de .wmv-films"
if [[ -d "$GST" ]]; then
  N=$(ls -1 "$GST"/libgst*.so 2>/dev/null | wc -l)
  [[ -r "$GST/libgstasf.so" ]]   && ok "libgstasf.so aanwezig ($(stat -c%s "$GST/libgstasf.so") B)" \
                                 || mis "libgstasf.so ontbreekt -- zonder ASF-demuxer stopt de game bij de eerste film"
  [[ -r "$GST/libgstlibav.so" ]] && ok "libgstlibav.so aanwezig ($(stat -c%s "$GST/libgstlibav.so") B)" \
                                 || mis "libgstlibav.so ontbreekt -- zonder WMV-decoder stopt de game bij de eerste film"
  [[ "$N" == 2 ]] && ok "precies 2 plug-ins, geen rommel" || wrs "$N plug-ins in de map (verwacht 2)"
else
  mis "plug-insmap ontbreekt: $GST (scripts/fetch-gst-wmv.sh)"
fi
if [[ -f "$U/UserProfiles/LuigisMansion.env" ]]; then
  grep -q "^GST_PLUGIN_PATH=" "$U/UserProfiles/LuigisMansion.env" \
    && ok "GST_PLUGIN_PATH staat in UserProfiles/LuigisMansion.env" \
    || wrs "GST_PLUGIN_PATH niet in de .env (onze eigen route zet hem via [Wine] GStreamerPlugins)"
fi
[[ -f "$(dirname "$PFX")/../wine/proton-11.0-2/lib/wine/x86_64-unix/winegstreamer.so" ]] \
  && ok "winegstreamer aanwezig in de Wine-build" || wrs "winegstreamer niet gevonden -- controleer de Wine-build"
WS="$HOME/.local/share/ravengun/wine/proton-11.0-2/lib/wine/x86_64-unix/winegstreamer.so"
[[ -r "$WS" ]] && ok "winegstreamer.so aanwezig (de .wmv's gaan hierdoorheen)" \
               || mis "winegstreamer.so ontbreekt: dan blijven de films zwart"

kop "4. De Wine-prefix en COM1"
if [[ -d "$PFX" ]]; then
  ok "prefix: $PFX"
  if [[ -L "$PFX/dosdevices/com1" ]]; then
    DOEL=$(readlink "$PFX/dosdevices/com1")
    ok "dosdevices/com1 -> $DOEL"
    if [[ "$DOEL" == /dev/ttyS* ]]; then
      if [[ -r "$DOEL" && -w "$DOEL" ]]; then
        wrs "$DOEL is leesbaar EN beschrijfbaar: de game opent dan een ECHTE seriele poort"
      else
        RE=$(stat -c '%U:%G %a' "$DOEL" 2>/dev/null)
        wrs "$DOEL is niet toegankelijk voor $(id -un) ($RE): de game krijgt ACCESS_DENIED (5),"
        printf '        niet FILE_NOT_FOUND (2). Dit is het verschil dat patch 0005 wilde wegnemen.\n'
      fi
    elif [[ "$DOEL" == /dev/pts/* || -L "$PFX/dosdevices/com1" && "$DOEL" == *luigi-com1* ]]; then
      wrs "com1 wijst naar een pty -- er staat nog een meetopstelling. Voor een schone meting: weghalen."
    fi
  else
    wrs "dosdevices/com1 bestaat niet (Wine maakt die pas bij het starten van de prefix)"
  fi
  [[ -x "$PFX/../wine/../../../wine/proton-11.0-2/bin/wine" ]] || true
else
  mis "prefix ontbreekt: $PFX"
fi
if [[ -r /dev/shm/ravengun_jvs_modem ]]; then
  ok "modemstatus-bestand aanwezig (de comm-shim gebruikt het)"
else
  wrs "geen /dev/shm/ravengun_jvs_modem: de comm-shim meldt dan CTS+DSR aan (meestal goed)"
fi
SHIM_GEOK=""
for c in "$HOME/projects/ravengun/build/librg_comm_shim.so" \
         "$HOME/.local/share/ravengun/runtime/lib/librg_comm_shim.so"; do
  [[ -r "$c" ]] && SHIM_GEOK="$c" && break
done
[[ -n "$SHIM_GEOK" ]] && ok "comm-shim: $SHIM_GEOK (nodig als com1 een pty is)" \
  || wrs "librg_comm_shim.so niet gevonden (cmake --build build): GetCommModemStatus faalt op een pty"

kop "5. De bench is vrij"
BZ=$(pgrep -a -f "VACUUM.exe|OpenParrotLoader|BudgieLoader|wine-preloader" 2>/dev/null | head -5)
if [[ -n "$BZ" ]]; then
  wrs "er draait nog iets:"
  printf '%s\n' "$BZ" | sed 's/^/        /'
  while read -r pid cmd; do
    T=$(awk '{print $20}' "/proc/$pid/stat" 2>/dev/null)
    C=$(awk '{print $14+$15}' "/proc/$pid/stat" 2>/dev/null)
    sleep 1
    C2=$(awk '{print $14+$15}' "/proc/$pid/stat" 2>/dev/null)
    [[ -n "$C" && -n "$C2" ]] && printf '        pid %s: %s threads, %s ticks CPU/s%s\n' \
      "$pid" "${T:-?}" "$((C2-C))" "$( [[ $((C2-C)) -gt 80 ]] && echo '  <- SPINT (vastgelopen rest, opruimen met kill -9)' )"
  done <<< "$BZ"
else
  ok "geen game-, lader- of wine-proces van een vorige run"
fi
pgrep -x wineserver >/dev/null 2>&1 && ok "wineserver draait (prima: com1 blijft dan staan)" \
  || wrs "geen wineserver: Wine zet com1..com32 opnieuw zodra hij start"

kop "6. TP_*-variabelen (die veranderen de route van de lader)"
TPV=$(env | grep -E '^TP_(REMOTETHREAD|POSTSTART_REMOTETHREAD_MS|ENTRYPOINT_REMOTETHREAD_MS|LOADER_MANAGED_INIT|PRELOAD_DLL|GAME_WORKING_DIRECTORY)=' || true)
if [[ -n "$TPV" ]]; then
  wrs "deze staan in je omgeving en lekken zo de game in:"
  printf '%s\n' "$TPV" | sed 's/^/        /'
  printf '        launch-direct.py laat TP_REMOTETHREAD=off juist weg; met de hand starten betekent: unset.\n'
else
  ok "geen TP_*-injectievariabelen in de omgeving"
fi
if [[ -f "$U/UserProfiles/LuigisMansion.env" ]]; then
  grep -q "^TP_REMOTETHREAD=off" "$U/UserProfiles/LuigisMansion.env" \
    && ok "UserProfiles/LuigisMansion.env zegt TP_REMOTETHREAD=off (entry-point-route)" \
    || wrs "TP_REMOTETHREAD=off staat niet in de .env: de lader neemt dan de remote-thread-route,"
  printf '        en dat is de route waarop deze game eerder omviel.\n'
fi

kop "7. Welke kern er werkelijk gebruikt wordt"
if [[ "${1:-}" == "--kern" || "${RG_KERN:-0}" == 1 ]]; then
  ENVF="$U/UserProfiles/LuigisMansion.env"
  DEF="$HOME/projects/ravengun/patches/_defaults/TeknoParrot.ini"
  [[ -f "$DEF" ]] && ok "_defaults/TeknoParrot.ini: $(grep -E '^(RemoteThread|Build|Core)' "$DEF" | tr '\n' ' ')"
  GI=$(ls "$HOME/projects/ravengun-patches/patches/LuigisMansion/game.ini" \
          "$HOME/.local/share/ravengun/patches/LuigisMansion/game.ini" 2>/dev/null | head -1)
  if [[ -n "$GI" ]]; then
    L=$(grep -A4 '^\[Loader\]' "$GI" 2>/dev/null | tr '\n' ' ')
    [[ -n "$L" ]] && ok "game.ini [Loader]: $L" || ok "game.ini heeft geen [Loader]: de _defaults gelden (eigen lader, GESLOTEN kern)"
  fi
  if [[ -d "$OPC/OpenParrotx64" ]]; then
    ok "eigen build staat klaar: $OPC"
    sha256sum "$OPC/OpenParrotx64/OpenParrot64.dll" "$OPC/OpenParrotx64/OpenParrotLoader64.exe" 2>/dev/null | sed 's/^/        /'
    grep -c '^  [0-9a-f]\{64\}' "$OPC/BUILDINFO.txt" >/dev/null 2>&1 && \
      ok "BUILDINFO: $(grep -c '\.patch$' "$OPC/BUILDINFO.txt") patches erin"
  fi
  printf '        Onthoud: de meting van ronde 57 is dat ONZE lader met de GESLOTEN kern werkt,\n'
  printf '        en dat dezelfde lader met ONZE kern (OpenParrot64 i.p.v. TeknoParrot64 als\n'
  printf '        EERSTE argument) exitcode 32 geeft. Voor Type = TeknoParrot weigert launch-direct.py\n'
  printf '        [Loader] Core = ravengun, dus die vergelijking doe je met de hand. Noteer welke DLL.\n'
else
  printf '  (draai met --kern voor de lader/kern-keuze en de hashes)\n'
fi

kop "8. NAS alleen-lezen (de regel)"
if [[ -d "$GAME" ]]; then
  # Geen schrijfproef: dat zou de NAS aanraken. We kijken alleen naar de rechten.
  if [[ -w "$GAME" ]]; then
    wrs "de NAS-map is voor jou BESCHRIJFBAAR -- wij horen daar alleen te lezen; wees hier voorzichtig"
  else
    ok "de NAS-map is niet beschrijfbaar voor $(id -un) (goed)"
  fi
  mount | grep -E "spidernas|$GAME" | head -3 | sed 's/^/        /'
else
  wrs "de NAS-map is niet gemount op $GAME"
fi

kop "Uitkomst"
if [[ $MIS -gt 0 ]]; then
  printf '  \033[31m%d blokkerende punten\033[0m, %d waarschuwingen. Een run is nu weggegooid geld.\n' "$MIS" "$WAARSCH"
  exit 1
elif [[ $WAARSCH -gt 0 ]]; then
  printf '  \033[33m0 blokkerende punten, %d waarschuwingen.\033[0m Een mislukte start ligt dan aan de kern, niet aan de omgeving.\n' "$WAARSCH"
  exit 0
else
  printf '  \033[32mAlles groen.\033[0m Een mislukte start ligt aan de kern, niet aan de omgeving.\n'
  exit 0
fi

