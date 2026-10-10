# Gedeelde padopzoeking voor de scripts (met `.` ingelezen, geen eigen programma). Vereist: ROOT = de datamap (de map boven
# scripts/). Zet:
#   RG_PREFIX  de installatie (<prefix>, met bin/, lib/ en share/ravengun = ROOT), leeg bij ontwikkeling in de repo
#   RG_BIN     de map met ravengunctl en de andere programma's  (installatie: <prefix>/bin, repo: <repo>/build)
#   RG_LIB     de map met librg_comm_shim.so, lib32/ en librg_rt_input.so  (installatie: <prefix>/lib, repo: <repo>/build)
# $RAVENGUN_BIN_DIR en $RAVENGUN_LIB_DIR gaan voor. Zie docs/build.md, "Installatie-indeling".
RG_PREFIX=""
if [[ "$(basename "$(dirname "$ROOT")")" == share && -d "$ROOT/../../bin" ]]; then
  RG_PREFIX="$(cd "$ROOT/../.." && pwd)"
fi
if [[ -n "$RG_PREFIX" ]]; then
  RG_BIN="${RAVENGUN_BIN_DIR:-$RG_PREFIX/bin}"; RG_LIB="${RAVENGUN_LIB_DIR:-$RG_PREFIX/lib}"
else
  RG_BIN="${RAVENGUN_BIN_DIR:-$ROOT/build}"; RG_LIB="${RAVENGUN_LIB_DIR:-$ROOT/build}"
fi
# Uitvoermap van de Unity-plugin (scripts/build-unity-plugin.sh): in de repo build/unity-plugin, bij een installatie onder de gebruikersdata.
if [[ -n "$RG_PREFIX" ]]; then RG_PLUGIN_OUT="${RAVENGUN_UNITY_PLUGIN_OUT:-${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/unity-plugin}"
else RG_PLUGIN_OUT="${RAVENGUN_UNITY_PLUGIN_OUT:-$ROOT/build/unity-plugin}"; fi
