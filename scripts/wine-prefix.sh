#!/usr/bin/env bash
# WineRunnerPath-wrapper voor TeknoParrot: draait wine met een eigen prefix (bv. een kopie met extra
# redistributables) in plaats van de prefix die TeknoParrot opgeeft. Geldt voor de game en voor pipehelper.exe.
# Omgeving: RG_WINEPREFIX=pad (verplicht), RG_WINE=pad naar de echte wine (standaard Kron4ek proton-11.0-2).
REAL="${RG_WINE:-$HOME/.local/share/ravengun/wine/proton-11.0-2/bin/wine}"
# RG_LD_PRELOAD=pad: LD_PRELOAD voor alleen de wine-processen (de jvs-route: build/librg_comm_shim.so).
[[ -n "${RG_LD_PRELOAD:-}" ]] && export LD_PRELOAD="$RG_LD_PRELOAD${LD_PRELOAD:+:$LD_PRELOAD}"
export WINEPREFIX="${RG_WINEPREFIX:?RG_WINEPREFIX niet gezet}"
export WINESERVER="$(dirname "$REAL")/wineserver"
exec "$REAL" "$@"
