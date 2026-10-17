#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Proef van rg-amhw.dll onder Wine ZONDER spel: bouwt de DLL en tools/rg-amhw/amhw_probe.cpp in de MinGW-container en draait de proef in een
# wegwerp-prefix (eigen wineserver, geen verband met een spelprefix). Nodig: podman en een Wine (standaard die van RavenGun).
#
# Gebruik: tools/dev/test-amhw-wine.sh [OEM VENDOR]     (standaard AAM Supermicro)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WINE="${RG_TEST_WINE:-$HOME/.local/share/ravengun/wine/proton-11.0-2/bin/wine}"
[[ -x "$WINE" ]] || { echo "geen Wine op $WINE (RG_TEST_WINE zetten)"; exit 77; }
command -v podman >/dev/null || { echo "geen podman"; exit 77; }
CONTAINERFILE="$ROOT/packaging/containers/openparrot/Containerfile"
IMAGE="localhost/ravengun-openparrot-build:$(sha256sum "$CONTAINERFILE" | cut -c1-12)"
podman image exists "$IMAGE" || { echo "image $IMAGE ontbreekt (tools/dev/build-amhw.sh bouwt hem)"; exit 77; }
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/src" "$TMP/out" "$TMP/prefix" "$TMP/run"
cp "$ROOT/tools/rg-amhw/"{rg_amhw.cpp,amhw_dmi.h,amhw_jvs.h,amhw_probe.cpp} "$TMP/src/"
podman run --rm --network=none -v "$TMP/src:/src:ro" -v "$TMP/out:/out" "$IMAGE" bash -c '
  i686-w64-mingw32-g++ -std=c++17 -O2 -s -shared -Wall -Wextra -static -static-libgcc -static-libstdc++ /src/rg_amhw.cpp -o /out/rg-amhw.dll &&
  i686-w64-mingw32-g++ -std=c++17 -O2 -s -Wall -Wextra -static -static-libgcc -static-libstdc++ /src/amhw_probe.cpp -o /out/amhw_probe.exe'
cp "$TMP/out/rg-amhw.dll" "$TMP/out/amhw_probe.exe" "$TMP/run/"
export WINEPREFIX="$TMP/prefix" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
unset DISPLAY WAYLAND_DISPLAY
OUT="$(timeout 120 "$WINE" "$TMP/run/amhw_probe.exe" 'Z:'"${TMP//\//\\}"'\run' "${1:-AAM}" "${2:-Supermicro}" 2>&1)" || RC=$?
echo "$OUT"
echo "--- rg-amhw.log ---"
cat "$TMP/run/rg-amhw.log" 2>/dev/null || echo "(geen logboek)"
"$WINE"server -k >/dev/null 2>&1 || true
exit "${RC:-0}"
