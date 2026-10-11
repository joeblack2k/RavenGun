#!/usr/bin/env bash
# WineRunnerPath-wrapper voor TeknoParrot: draait wine met een eigen prefix (bv. een kopie met extra
# redistributables) in plaats van de prefix die TeknoParrot opgeeft. Geldt voor de game en voor pipehelper.exe.
# Omgeving: RG_WINEPREFIX=pad (verplicht), RG_WINE=pad naar de echte wine (standaard Kron4ek proton-11.0-2).
REAL="${RG_WINE:-$HOME/.local/share/ravengun/wine/proton-11.0-2/bin/wine}"
# RG_LD_PRELOAD=pad: LD_PRELOAD voor alleen de wine-processen (de jvs-route: build/librg_comm_shim.so).
[[ -n "${RG_LD_PRELOAD:-}" ]] && export LD_PRELOAD="$RG_LD_PRELOAD${LD_PRELOAD:+:$LD_PRELOAD}"
# RG_UNSET_REMOTETHREAD=1: TeknoParrotUi zet onder Wine zelf TP_REMOTETHREAD; weglaten geeft de standaardroute van de lader
# (entry-point-trampoline), de enige die de kern bij o.a. HOTD: Scarlet Dawn laat aanhaken.
[[ "${RG_UNSET_REMOTETHREAD:-}" == 1 ]] && unset TP_REMOTETHREAD
export WINEPREFIX="${RG_WINEPREFIX:?RG_WINEPREFIX niet gezet}"
export WINESERVER="$(dirname "$REAL")/wineserver"
exec "$REAL" "$@"
