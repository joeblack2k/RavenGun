#!/usr/bin/env bash
# Bouwt de Qt-app `ravengun` (src/app) in een rootless podman-container met Qt 6.11 (zelfde hoofd/minor-versie als de
# systeem-Qt van SteamOS, die geen headers heeft). De binary komt in build-app/ravengun en draait op de host tegen de
# systeem-Qt. Zie docs/build.md.
#
# Gebruik: scripts/build-app.sh [--test] [--rebuild-image] [--clean]
#   --test           ook de app-tests bouwen en draaien (in de container, offscreen) en daarna op de host
#   --rebuild-image  de container-image opnieuw bouwen (packaging/containers/qt-app/Containerfile)
#   --clean          build-app/ eerst leegmaken
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE=localhost/ravengun-qt-build
BUILD="$ROOT/build-app"
TEST=0; REBUILD=0; CLEAN=0
for a in "$@"; do
  case "$a" in
    --test) TEST=1 ;;
    --rebuild-image) REBUILD=1 ;;
    --clean) CLEAN=1 ;;
    *) echo "Gebruik: $0 [--test] [--rebuild-image] [--clean]" >&2; exit 2 ;;
  esac
done

if [[ $REBUILD -eq 1 ]] || ! podman image exists "$IMAGE"; then
  podman build -t "$IMAGE" -f "$ROOT/packaging/containers/qt-app/Containerfile" "$ROOT/packaging/containers/qt-app"
fi
[[ $CLEAN -eq 1 ]] && rm -rf "$BUILD"
mkdir -p "$BUILD"

TARGETS="ravengun_app"
[[ $TEST -eq 1 ]] && TARGETS="ravengun_app ravengun_app_tests ravengun_components_tests ravengun_app_widget_tests"
# De repo wordt (op hetzelfde pad) gemount, plus de patches-repo als patches/ daarheen wijst (../ravengun-patches/patches; anders
# is de koppeling in de container leeg en falen de app-tests die de meegeleverde game.ini lezen). Geen netwerk, image ongewijzigd.
MOUNTS=(-v "$ROOT:$ROOT")
PATCHES_REAL="$(realpath -e "$ROOT/patches" 2>/dev/null || true)"
[[ -n "$PATCHES_REAL" && "$PATCHES_REAL" != "$ROOT"/* ]] && MOUNTS+=(-v "$PATCHES_REAL:$PATCHES_REAL:ro")
podman run --rm --network=none --userns=keep-id "${MOUNTS[@]}" -w "$ROOT" -e HOME=/tmp "$IMAGE" sh -c "
  set -e
  cmake -S '$ROOT' -B '$BUILD' -G Ninja -DCMAKE_BUILD_TYPE=Release -DRAVENGUN_BUILD_DAEMON=OFF -DRAVENGUN_BUILD_CAMPROBE=OFF >/dev/null
  cmake --build '$BUILD' --target $TARGETS -- -k 0
  if [ $TEST -eq 1 ]; then QT_QPA_PLATFORM=offscreen '$BUILD/ravengun_app_tests' && QT_QPA_PLATFORM=offscreen '$BUILD/ravengun_components_tests' && QT_QPA_PLATFORM=offscreen RAVENGUN_NO_GAMEPAD=1 '$BUILD/ravengun_app_widget_tests'; fi
"
echo "Gebouwd: $BUILD/ravengun"

# Controle op de host: alle bibliotheken gevonden (systeem-Qt 6.11) en de versie.
if ldd "$BUILD/ravengun" | grep -q 'not found'; then
  echo "Ontbrekende bibliotheken op de host:" >&2
  ldd "$BUILD/ravengun" | grep 'not found' >&2
  exit 1
fi
ldd "$BUILD/ravengun" | grep -E 'libQt6(Core|Widgets)' || true
if [[ $TEST -eq 1 ]]; then
  echo "App-tests op de host (systeem-Qt):"
  QT_QPA_PLATFORM=offscreen "$BUILD/ravengun_app_tests"
  QT_QPA_PLATFORM=offscreen "$BUILD/ravengun_components_tests"
  QT_QPA_PLATFORM=offscreen RAVENGUN_NO_GAMEPAD=1 "$BUILD/ravengun_app_widget_tests"
fi
