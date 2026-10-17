#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Bouwt een losse BepInEx-plugin voor de directe route ([Launch] BepInEx, docs/per-game-patches.md): plugins/bepinex/<Game>/*.cs,
# in dezelfde rootless podman-container (Mono-compiler) als scripts/build-unity-plugin.sh. Anders dan de plugin van de Unity-route
# heeft zo'n plugin geen kanaal naar de daemon; hij past de game aan (Men in Black: niet zelf stoppen zonder kast-shell) of leest het
# bordbeeld van RavenGun in het spelproces (de WartranTroopers-familie: plugins/bepinex/shared/Bordbeeld.cs, altijd meegebouwd).
#
# Gebruik: scripts/build-bepinex-plugin.sh GAME [MANAGED_DIR]
#   MANAGED_DIR  de map <Spel>_Data/Managed van de game (alleen gelezen); standaard de view van de game
#                (~/.local/share/ravengun/runtime/views/<Game>/) plus het pad in plugins/bepinex/<Game>/managed.txt
# Optioneel plugins/bepinex/<Game>/refs.txt: per regel een assembly uit MANAGED_DIR waartegen gebouwd wordt (bv. Assembly-CSharp.dll,
# als de plugin typen van de game zelf gebruikt, zoals een enum als resultaat van een gehaakte methode).
# Resultaat: build/bepinex-plugin/<Game>/RavenGun.<Game>.dll. Dat is onze eigen build: als drop-in naar
# patches/<Game>/dropin/ (dropin.ini: To = BepInEx/plugins, relatief aan de map van de exe).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${RAVENGUN_UNITY_IMAGE:-localhost/ravengun-unity-build}"
BEPINEX="${RAVENGUN_BEPINEX:-${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/bepinex/5.4.21.0/x64/BepInEx/core}"
G="${1:?gebruik: scripts/build-bepinex-plugin.sh GAME [MANAGED_DIR]}"
SRC="$ROOT/plugins/bepinex/$G"
[[ -d $SRC ]] || { echo "geen bron in $SRC" >&2; exit 1; }
MANAGED="${2:-}"
if [[ -z $MANAGED ]]; then
  [[ -f $SRC/managed.txt ]] || { echo "$SRC/managed.txt ontbreekt; geef MANAGED_DIR op" >&2; exit 1; }
  MANAGED="${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/runtime/views/$G/$(tr -d '\r\n' < "$SRC/managed.txt")"
fi
# Unity 2017.2 en nieuwer: UnityEngine.CoreModule.dll; Unity 5 (Wild West Shootout): alleen UnityEngine.dll.
[[ -f $MANAGED/UnityEngine.CoreModule.dll || -f $MANAGED/UnityEngine.dll ]] || { echo "$MANAGED/UnityEngine(.CoreModule).dll niet gevonden (eerst een keer starten voor de view, of MANAGED_DIR opgeven)" >&2; exit 1; }
[[ -f $BEPINEX/BepInEx.dll && -f $BEPINEX/0Harmony.dll ]] || { echo "BepInEx niet gevonden in $BEPINEX (scripts/fetch-bepinex.sh)" >&2; exit 1; }
if ! podman image exists "$IMAGE"; then
  podman build -q -t "$IMAGE" -f "$ROOT/packaging/containers/unity-plugin/Containerfile" "$ROOT/packaging/containers/unity-plugin"
fi
OUT="$ROOT/build/bepinex-plugin/$G"
mkdir -p "$OUT"
# De links van een view volgen: podman mount de echte map (op de NAS, alleen gelezen).
MANAGED="$(realpath "$MANAGED")"
podman run --rm --network=none \
  -v "$SRC:/src:ro" -v "$ROOT/plugins/bepinex/shared:/shared:ro" -v "$MANAGED:/managed:ro" -v "$BEPINEX:/bepinex:ro" -v "$OUT:/out" \
  "$IMAGE" sh -c '
    set -e
    refs=""
    for f in /managed/UnityEngine*.dll /managed/netstandard.dll /managed/System.dll /managed/System.Core.dll; do
      if [ -f "$f" ]; then refs="$refs -r:$f"; fi
    done
    if [ -f /src/refs.txt ]; then
      for n in $(tr -d "\r" < /src/refs.txt); do refs="$refs -r:/managed/$n"; done
    fi
    mcs -target:library -optimize+ -debug- -nostdlib -noconfig -warn:3 -r:/managed/mscorlib.dll $refs \
      -r:/bepinex/BepInEx.dll -r:/bepinex/0Harmony.dll -out:/out/RavenGun.'"$G"'.dll /src/*.cs /shared/*.cs
  '
ls -la "$OUT/RavenGun.$G.dll"
echo "drop-in: cp $OUT/RavenGun.$G.dll patches/$G/dropin/ (dropin.ini: To = BepInEx/plugins, naast de exe)"
