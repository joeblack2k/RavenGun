#!/usr/bin/env bash
# Bouwt het releasearchief ravengun-<versie>-x86_64-steamos.tar.gz (met SHA-256) in rootless podman-containers, zonder netwerk tijdens het
# bouwen en alleen uit de gecommitte bronnen (git archive van HEAD) plus PSMoveAPI op de vastgepinde commit. Zie docs/build.md,
# "Release-artefact".
#
# Gebruik: scripts/make-release.sh [--version V] [--out MAP] [--rebuild-image] [--no-test] [--allow-dirty] [--psmoveapi-src MAP]
#   --version V         versie in de bestandsnaam (standaard: project(VERSION) uit CMakeLists.txt)
#   --out MAP           uitvoermap (standaard dist/)
#   --rebuild-image     de container-images opnieuw bouwen
#   --no-test           de ctest-suites in de container overslaan
#   --allow-dirty       ook bouwen als er niet-gecommitte wijzigingen zijn (die komen NIET in het archief: alleen HEAD)
#   --psmoveapi-src MAP een git-kloon van thp/psmoveapi (standaard ~/.cache/ravengun/src/psmoveapi; ontbreekt die, dan wordt hij gekloond)
#
# Reproduceerbaar: vaste containerpaden (/work), SOURCE_DATE_EPOCH = de commit-tijd van HEAD, tar met vaste volgorde, eigenaar en
# tijdstempels, gzip zonder naam/tijd. Twee keer bouwen van dezelfde commit geeft dezelfde SHA-256 (zie docs/build.md voor de
# voorwaarden: dezelfde images).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG_QT=localhost/ravengun-qt-build
IMG=localhost/ravengun-release-build
PSM_PIN="$(sed -n 's/^set(RAVENGUN_PSMOVEAPI_PIN "\([0-9a-f]*\)".*/\1/p' "$ROOT/CMakeLists.txt")"
VERSION="$(sed -n 's/^project(RavenGun VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
OUT="$ROOT/dist"; REBUILD=0; TEST=1; DIRTY=0
PSM_SRC="${RAVENGUN_PSMOVEAPI_SRC:-$HOME/.cache/ravengun/src/psmoveapi}"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) VERSION="$2"; shift ;;
    --out) OUT="$2"; shift ;;
    --rebuild-image) REBUILD=1 ;;
    --no-test) TEST=0 ;;
    --allow-dirty) DIRTY=1 ;;
    --psmoveapi-src) PSM_SRC="$2"; shift ;;
    -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
    *) echo "Gebruik: $0 [--version V] [--out MAP] [--rebuild-image] [--no-test] [--allow-dirty] [--psmoveapi-src MAP]" >&2; exit 2 ;;
  esac
  shift
done
[[ -n $PSM_PIN && -n $VERSION ]] || { echo "Kan versie of PSMoveAPI-pin niet uit CMakeLists.txt lezen" >&2; exit 1; }
cd "$ROOT"
if [[ -n "$(git status --porcelain --untracked-files=no)" && $DIRTY -eq 0 ]]; then
  echo "Er zijn niet-gecommitte wijzigingen; het archief bevat alleen HEAD. Commit ze, of gebruik --allow-dirty." >&2; exit 1
fi
COMMIT="$(git rev-parse HEAD)"
SDE="$(git log -1 --format=%ct HEAD)"
NAME="ravengun-$VERSION-x86_64-steamos"
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"

# --- Bronnen -----------------------------------------------------------------------------------------------------------------
WORK="$(mktemp -d "${TMPDIR:-/tmp}/ravengun-release.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/src" "$WORK/psmoveapi-src" "$WORK/stage"
git archive --format=tar HEAD | tar -x -C "$WORK/src"
if [[ ! -d $PSM_SRC/.git ]]; then
  echo "PSMoveAPI-bron ontbreekt; klonen naar $PSM_SRC"
  mkdir -p "$(dirname "$PSM_SRC")"; git clone https://github.com/thp/psmoveapi "$PSM_SRC"
fi
git -C "$PSM_SRC" cat-file -e "$PSM_PIN^{commit}" 2>/dev/null || git -C "$PSM_SRC" fetch --quiet origin
git -C "$PSM_SRC" archive --format=tar "$PSM_PIN" | tar -x -C "$WORK/psmoveapi-src"
cp "$WORK/psmoveapi-src/COPYING" "$WORK/PSMoveAPI-COPYING"

# --- Images ------------------------------------------------------------------------------------------------------------------
if [[ $REBUILD -eq 1 ]] || ! podman image exists "$IMG_QT"; then
  podman build -t "$IMG_QT" -f "$ROOT/packaging/containers/qt-app/Containerfile" "$ROOT/packaging/containers/qt-app"
fi
if [[ $REBUILD -eq 1 ]] || ! podman image exists "$IMG"; then
  podman build -t "$IMG" -f "$ROOT/packaging/containers/release/Containerfile" "$ROOT/packaging/containers/release"
fi

# --- Bouwen in de container (geen netwerk, alleen $WORK) ----------------------------------------------------------------------
podman run --rm --network=none --userns=keep-id -v "$WORK:/work" -w /work -e HOME=/tmp -e SOURCE_DATE_EPOCH="$SDE" \
  -e RG_COMMIT="${COMMIT:0:12}" -e RG_NAME="$NAME" -e RG_TEST="$TEST" -e RG_PSM_PIN="$PSM_PIN" "$IMG" bash -euo pipefail -c '
  P=/work/stage/$RG_NAME
  # 1. PSMoveAPI (alleen de bibliotheek; opties en reden: docs/build.md, "PSMoveAPI (in een container)")
  mkdir -p /tmp/pc && cp /work/src/cmake/container/glm.pc /tmp/pc/
  PKG_CONFIG_PATH=/tmp/pc cmake -S /work/psmoveapi-src -B /work/build/psmoveapi -G Ninja \
    -DPSMOVEAPI_VENDORING=OFF -DPSMOVE_BUILD_TRACKER=OFF -DPSMOVE_BUILD_NAVCON_TEST=OFF -DPSMOVE_USE_SIXPAIR=OFF \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON "-DCMAKE_INSTALL_RPATH=\$ORIGIN/../lib" >/dev/null
  cmake --build /work/build/psmoveapi --target psmoveapi
  mkdir -p /work/psm/lib /work/psm/include/psmoveapi
  cp /work/build/psmoveapi/libpsmoveapi.so /work/psm/lib/
  cp /work/psmoveapi-src/include/*.h /work/build/psmoveapi/psmove_config.h /work/psm/include/psmoveapi/
  # 2. RavenGun: engine, CLI, hulpen en de Qt-app, met de unit-tests
  cmake -S /work/src -B /work/build/rg -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSMOVEAPI_ROOT=/work/psm \
    -DRAVENGUN_GIT_COMMIT_OVERRIDE="$RG_COMMIT" -DRAVENGUN_PSMOVEAPI_PIN="$RG_PSM_PIN" >/dev/null
  cmake --build /work/build/rg
  if [ "$RG_TEST" = 1 ]; then
    QT_QPA_PLATFORM=offscreen RAVENGUN_NO_GAMEPAD=1 ctest --test-dir /work/build/rg --output-on-failure -j4
  fi
  cmake --install /work/build/rg --prefix "$P" >/dev/null
  # 3. Eisen van deze build: de hoogste glibc-symboolversie in de programma en bibliotheken.
  need=$(for f in "$P"/bin/* "$P"/lib/*.so "$P"/lib/lib32/*.so; do [ -f "$f" ] && objdump -T "$f" 2>/dev/null; done | grep -o "GLIBC_[0-9.]*" | sort -uV | tail -1)
  printf "glibc %s\n" "${need#GLIBC_}" > "$P/share/ravengun/REQUIRES"
  printf "Qt 6.11 or newer (system), hidapi, BlueZ, dbus, libudev (system)\n" >> "$P/share/ravengun/REQUIRES"
'
[[ -x $WORK/stage/$NAME/bin/ravengund && -x $WORK/stage/$NAME/bin/ravengun ]] || { echo "Bouwen mislukt: ravengund of ravengun ontbreekt in de installatieboom" >&2; exit 1; }

# --- Archief samenstellen ------------------------------------------------------------------------------------------------------
S="$WORK/stage/$NAME"
install -m 755 "$ROOT/packaging/release/install.sh" "$S/install.sh"
cp "$S/share/ravengun/LICENSE" "$S/LICENSE"
cp "$ROOT/README.md" "$S/README.md" 2>/dev/null || true
mkdir -p "$S/share/ravengun/licenses"
cp "$WORK/PSMoveAPI-COPYING" "$S/share/ravengun/licenses/PSMoveAPI-COPYING"
printf '%s\n%s\n' "$VERSION" "$COMMIT" > "$S/share/ravengun/VERSION"
# Vaste rechten: mappen 755; bestanden 755 als ze uitvoerbaar zijn, anders 644.
find "$S" -type d -exec chmod 755 {} +
find "$S" -type f -perm -u+x -exec chmod 755 {} +
find "$S" -type f ! -perm -u+x -exec chmod 644 {} +
TARBALL="$OUT/$NAME.tar.gz"
tar --sort=name --mtime="@$SDE" --owner=0 --group=0 --numeric-owner -C "$WORK/stage" -cf - "$NAME" | gzip -n -9 > "$TARBALL"
# De bijbehorende bron (GPL): dezelfde commit.
SRCTAR="$OUT/ravengun-$VERSION-source.tar.gz"
git archive --format=tar --prefix="ravengun-$VERSION/" HEAD | gzip -n -9 > "$SRCTAR"
(cd "$OUT" && sha256sum "$NAME.tar.gz" "ravengun-$VERSION-source.tar.gz" > "$NAME.sha256")
echo
echo "Release: $TARBALL"
echo "Bron:    $SRCTAR"
echo "SHA-256: $OUT/$NAME.sha256"
cat "$OUT/$NAME.sha256"
echo "Vereist: $(head -1 "$S/share/ravengun/REQUIRES")"
