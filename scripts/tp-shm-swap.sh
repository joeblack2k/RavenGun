#!/usr/bin/env bash
# Hulp van de tp-shm-route (docs/decisions.md, "IO-brug: tp-shm"). Draait op de achtergrond in de unit ravengun-tpui, naast
# TeknoParrotUi (zie play-halo.sh). TeknoParrotUi start voor de game een Windows-hulpprogramma, pipehelper.exe, dat zijn eigen
# invoerbestand (/dev/shm/TeknoParrot_JvsState) naar de Windows-mapping "TeknoParrot_JvsState" spiegelt die de TeknoParrot-kern in
# de game leest. Dit script vervangt dat proces door hetzelfde programma met dezelfde argumenten en omgeving, maar met het bestand
# van de daemon (/dev/shm/ravengun_JvsState) als bron. Zo is de daemon de enige schrijver van de invoer; TeknoParrotUi blijft
# de loader starten en de JVS-pipe bedienen, maar zijn eigen (lege of foute) invoer bereikt de game niet meer.
#
# Er wordt niets aan TeknoParrot zelf gewijzigd: alleen een pipehelper.exe van de eigen sessie wordt opnieuw gestart. Het
# programma is dat van TeknoParrotUi (in zijn map), niet van ons; zie docs/decisions.md over licentie en clean-room.
set -u
SHM=/dev/shm/ravengun_JvsState
OLD='Z:\dev\shm\TeknoParrot_JvsState'
NEW='Z:\dev\shm\ravengun_JvsState'
log() { echo "[tp-shm-swap $(date +%H:%M:%S.%N | cut -c1-12)] $*"; }

cg=$(sed -n 's/^0:://p' /proc/self/cgroup)
PROCS="/sys/fs/cgroup$cg/cgroup.procs"
[[ -r "$PROCS" ]] || { log "geen cgroup.procs ($PROCS): niets vervangen"; exit 1; }

# Wachten op pipehelper.exe van TeknoParrotUi (hoogstens 90 s; hij start meteen na TeknoParrotUi).
FOUND=""
for _ in $(seq 1 1800); do
  for p in $(cat "$PROCS" 2>/dev/null); do
    { read -r c < "/proc/$p/comm"; } 2>/dev/null || continue
    [[ "$c" == "pipehelper.exe" ]] || continue
    if tr '\0' ' ' < "/proc/$p/cmdline" 2>/dev/null | grep -q 'ravengun_JvsState'; then continue; fi
    FOUND=$p; break 2
  done
  sleep 0.05
done
[[ -n "$FOUND" ]] || { log "geen pipehelper.exe gezien: niets vervangen"; exit 1; }

mapfile -d '' ARGS < "/proc/$FOUND/cmdline"
mapfile -d '' ENVRAW < "/proc/$FOUND/environ"
# De omgeving van een lopend wine-proces bevat dingen die wine zelf zet na het starten (de socket naar de wineserver als
# bestandsnummer, de preload-reserve): die horen niet in een nieuw proces.
ENVV=()
for e in "${ENVRAW[@]}"; do
  case "$e" in WINESERVERSOCKET=*|WINELOADERNOEXEC=*|WINEPRELOADRESERVE=*) ;; *) ENVV+=("$e") ;; esac
done
EXE=$(readlink "/proc/$FOUND/exe")
WINE="${EXE%/lib/wine/*}/bin/wine"
[[ -x "$WINE" ]] || { log "wine niet gevonden bij $EXE: niets vervangen"; exit 1; }
NEWARGS=(); HOSTARG=0
for a in "${ARGS[@]}"; do
  if [[ "${a,,}" == "${OLD,,}" ]]; then NEWARGS+=("$NEW"); HOSTARG=1; else NEWARGS+=("$a"); fi
done
[[ $HOSTARG -eq 1 ]] || { log "het bestandsargument van pipehelper.exe niet herkend (${ARGS[*]}): niets vervangen"; exit 1; }
for _ in $(seq 1 100); do [[ -e "$SHM" ]] && break; sleep 0.1; done
[[ -e "$SHM" ]] || { log "$SHM bestaat niet (de daemon maakt hem met 'ravengunctl iobridge start'): niets vervangen"; exit 1; }

log "pipehelper.exe pid $FOUND vervangen (wine $WINE)"
kill -KILL "$FOUND" 2>/dev/null
for _ in $(seq 1 100); do [[ -d "/proc/$FOUND" ]] || break; sleep 0.02; done
exec env -i "${ENVV[@]}" "$WINE" "${NEWARGS[@]}"
