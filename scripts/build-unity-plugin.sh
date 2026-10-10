#!/usr/bin/env bash
# Bouwt de RavenGun BepInEx-plugin (plugins/unity/) in een rootless podman-container (Mono-compiler).
#
# Gebruik: scripts/build-unity-plugin.sh [GAME|all] [--image-only]
#   GAME   sleutel uit packaging/aagames-games.json (TombRaider, RabbidsHollywood); standaard all
# Resultaat: build/unity-plugin/<GAME>/RavenGun.<GAME>.dll
#
# Referenties (read-only gemount, niets wordt gekopieerd of gewijzigd):
#  - BepInEx 5.4.21 (LGPL-2.1) + HarmonyX (MIT): ~/.local/share/ravengun/bepinex/5.4.21.0/x64/BepInEx/core
#    (zie PROVENANCE.txt daar; override met RAVENGUN_BEPINEX)
#  - de game-assemblies van de eigen dump op de NAS (<nas_dir>/Game_Data/Managed): Assembly-CSharp.dll e.d.
# De image wordt gebouwd uit packaging/containers/unity-plugin/Containerfile als hij nog niet bestaat.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${RAVENGUN_UNITY_IMAGE:-localhost/ravengun-unity-build}"
BEPINEX="${RAVENGUN_BEPINEX:-$HOME/.local/share/ravengun/bepinex/5.4.21.0/x64/BepInEx/core}"
OUT="$ROOT/build/unity-plugin"

WHICH="${1:-all}"
[[ "$WHICH" == --* ]] && WHICH=all
[[ -f "$BEPINEX/BepInEx.dll" && -f "$BEPINEX/0Harmony.dll" ]] || { echo "BepInEx niet gevonden in $BEPINEX (zie plugins/unity/README.md)" >&2; exit 1; }

if ! podman image exists "$IMAGE"; then
  echo "Image $IMAGE bouwen (eerste keer)..."
  podman build -q -t "$IMAGE" -f "$ROOT/packaging/containers/unity-plugin/Containerfile" "$ROOT/packaging/containers/unity-plugin"
fi
[[ " $* " == *" --image-only "* ]] && exit 0

# game-sleutel -> nas_dir en bronmap, uit het gedeelde JSON-bestand
game_info() {  # GAME veld
  python3 -I - "$ROOT/packaging/aagames-games.json" "$1" "$2" <<'PY'
import json, os, sys
cfg = json.load(open(sys.argv[1], encoding='utf-8'))
g = cfg['games'][sys.argv[2]]
if sys.argv[3] == 'nas_dir':
    print(os.path.expanduser(g['nas_dir']))
elif sys.argv[3] == 'src':
    print({'tomb_raider': 'tombraider', 'rabbids_hollywood': 'rabbids'}[g['type']])
PY
}

if [[ "$WHICH" == all ]]; then
  GAMES=$(python3 -I -c "import json,sys; print(' '.join(json.load(open(sys.argv[1]))['games']))" "$ROOT/packaging/aagames-games.json")
else
  GAMES="$WHICH"
fi

for G in $GAMES; do
  NAS="$(game_info "$G" nas_dir)"; SRC="$(game_info "$G" src)"
  MANAGED="$NAS/Game_Data/Managed"
  [[ -f "$MANAGED/Assembly-CSharp.dll" ]] || { echo "$G: $MANAGED/Assembly-CSharp.dll niet gevonden (NAS gekoppeld?)" >&2; exit 1; }
  mkdir -p "$OUT/$G"
  echo "== $G ($SRC)"
  podman run --rm --network=none \
    -v "$ROOT/plugins/unity/src:/src:ro" -v "$MANAGED:/managed:ro" -v "$BEPINEX:/bepinex:ro" -v "$OUT/$G:/out" \
    "$IMAGE" sh -c '
      set -e
      refs=""
      for f in /managed/*.dll; do
        case "$(basename "$f")" in mscorlib.dll|Assembly-UnityScript.dll|Boo.Lang.dll) continue ;; esac
        refs="$refs -r:$f"
      done
      mcs -target:library -optimize+ -debug- -nostdlib -noconfig -warn:3 \
        -r:/managed/mscorlib.dll $refs -r:/bepinex/BepInEx.dll -r:/bepinex/0Harmony.dll \
        -out:/out/RavenGun.'"$G"'.dll /src/common/*.cs /src/'"$SRC"'/*.cs
    '
  ls -la "$OUT/$G/RavenGun.$G.dll"
done
