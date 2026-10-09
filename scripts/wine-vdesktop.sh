#!/usr/bin/env bash
# WineRunnerPath-wrapper voor TeknoParrot: draait de game (alleen BudgieLoader*/OpenParrotLoader*) in een
# Wine-virtueel bureaublad (explorer /desktop=NAAM,BREEDTExHOOGTE) in plaats van op het echte scherm.
# Alles anders (pipehelper.exe, wineboot...) gaat ongewijzigd naar wine.
# Let op: de console-uitvoer van de loader komt niet in het TPUI-log (hij staat in het venster van het bureaublad).
# Omgeving: RG_VDESKTOP=1920x1080 (standaard), RG_WINE=pad naar de echte wine (standaard Kron4ek proton-11.0-2).
REAL="${RG_WINE:-$HOME/.local/share/ravengun/wine/proton-11.0-2/bin/wine}"
case "${1##*/}" in
  [Bb]udgie[Ll]oader*|[Oo]pen[Pp]arrot[Ll]oader*)
    exe="$1"; shift
    # explorer snapt geen Unix-pad als programma: omzetten naar Z:\
    [[ "$exe" == /* ]] && exe="Z:${exe//\//\\}"
    exec "$REAL" explorer "/desktop=RavenGun,${RG_VDESKTOP:-1920x1080}" "$exe" "$@" ;;
  *) exec "$REAL" "$@" ;;
esac
