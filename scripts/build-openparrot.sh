#!/usr/bin/env bash
# Bouwt de open OpenParrot-lader en -kern uit de openbare bron (GPL-3.0, github.com/teknogods/OpenParrot), met de Wine-fix
# als broncodewijziging, in een rootless podman-container (MinGW-w64 + clang). Zie third_party/openparrot/README.md.
#
# Gebruik: scripts/build-openparrot.sh [--targets "loader32 loader64 core32 core64"] [--out DIR] [--fetch-only] [--versions]
#   --targets     welke onderdelen (standaard alle vier): OpenParrotLoader.exe, OpenParrotLoader64.exe, OpenParrot.dll, OpenParrot64.dll
#   --out DIR     uitvoer (standaard ~/.local/share/ravengun/openparrot/<commit>/; $RAVENGUN_OPENPARROT_OUT)
#   --fetch-only  alleen de bron ophalen en controleren
#   --versions    commit, basis-image en pakketversies van de buildomgeving tonen
#
# Vastgepind: de bron (commit hieronder, gecontroleerd na het ophalen), de container (basis-image op digest, pakketten via een
# momentopname van het Arch Linux Archive; packaging/containers/openparrot/Containerfile) en onze patches
# (third_party/openparrot/patches/*.patch). De gedownloade bron wordt als onvertrouwd behandeld: alleen `git fetch` en
# `git archive`, daarna wordt er uitsluitend in de container, zonder netwerk, gebouwd (premake5.exe, *.bat en appveyor.yml
# uit de bron worden nooit uitgevoerd).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

OPENPARROT_REPO="https://github.com/teknogods/OpenParrot.git"
OPENPARROT_COMMIT="fc8ba632e1f67c5c340ff88f30efca743e8d38f5"   # 2026-09-11, "Add APM3 test support"
CONTAINERFILE="$ROOT/packaging/containers/openparrot/Containerfile"
SRC="${RAVENGUN_OPENPARROT_SRC:-$HOME/.cache/ravengun/src/openparrot}"
WORKROOT="${RAVENGUN_OPENPARROT_WORK:-$HOME/.cache/ravengun/build/openparrot}"
OUT="${RAVENGUN_OPENPARROT_OUT:-${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/openparrot/$OPENPARROT_COMMIT}"
TARGETS="loader32 loader64 core32 core64"
FETCH_ONLY=0; VERSIONS=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --targets) TARGETS="$2"; shift 2 ;;
    --out) OUT="$2"; shift 2 ;;
    --fetch-only) FETCH_ONLY=1; shift ;;
    --versions) VERSIONS=1; shift ;;
    -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
done

# --- 1. bron ophalen (vastgepinde commit) --------------------------------------------------------------------------
fetch_source() {
  mkdir -p "$SRC"
  if [[ ! -d "$SRC/.git" ]]; then
    git -C "$SRC" init -q
    git -C "$SRC" remote add origin "$OPENPARROT_REPO"
  fi
  if ! git -C "$SRC" cat-file -e "$OPENPARROT_COMMIT^{commit}" 2>/dev/null; then
    git -C "$SRC" fetch -q --depth 1 origin "$OPENPARROT_COMMIT"
  fi
  local got
  got="$(git -C "$SRC" rev-parse --verify "$OPENPARROT_COMMIT^{commit}")"
  [[ "$got" == "$OPENPARROT_COMMIT" ]] || { echo "bron: verkeerde commit $got (verwacht $OPENPARROT_COMMIT)" >&2; exit 1; }
  echo "bron: $SRC @ $got"
}

# --- 2. container -------------------------------------------------------------------------------------------------
IMAGE="localhost/ravengun-openparrot-build:$(sha256sum "$CONTAINERFILE" | cut -c1-12)"
ensure_image() {
  if ! podman image exists "$IMAGE"; then
    echo "Image $IMAGE bouwen (eerste keer; haalt de pakketten van het Arch Linux Archive)..."
    podman build -q -t "$IMAGE" -f "$CONTAINERFILE" "$(dirname "$CONTAINERFILE")" >/dev/null
  fi
}
toolchain_versions() {
  podman run --rm --network=none "$IMAGE" sh -c \
    'pacman -Q mingw-w64-gcc mingw-w64-headers mingw-w64-crt mingw-w64-winpthreads clang lld patch; echo "base-image: $(grep -m1 "^PRETTY_NAME" /etc/os-release)"'
}

if [[ $VERSIONS -eq 1 ]]; then
  echo "OpenParrot: $OPENPARROT_REPO @ $OPENPARROT_COMMIT"
  echo "Containerfile: $CONTAINERFILE (basis-image $(grep -m1 '^FROM' "$CONTAINERFILE" | cut -d' ' -f2), $(grep -m1 'ARG ALA_DATE' "$CONTAINERFILE"))"
  ensure_image; toolchain_versions
  exit 0
fi

fetch_source
[[ $FETCH_ONLY -eq 1 ]] && exit 0
ensure_image

# --- 3. bron klaarzetten (zonder git-map), onze patches toepassen -------------------------------------------------
WORK="$WORKROOT/$OPENPARROT_COMMIT"
rm -rf "$WORK"; mkdir -p "$WORK/src"
git -C "$SRC" archive "$OPENPARROT_COMMIT" | tar -x -C "$WORK/src" --no-same-owner --no-same-permissions
podman run --rm --network=none -v "$WORK/src:/src" -v "$ROOT/third_party/openparrot/patches:/patches:ro" "$IMAGE" sh -c '
  set -e; cd /src
  for p in /patches/*.patch; do echo "patch: $(basename "$p")"; patch -p1 --no-backup-if-mismatch -s < "$p"; done'

# --- 4. bouwen ---------------------------------------------------------------------------------------------------
mkdir -p "$OUT"
podman run --rm --network=none -e TARGETS="$TARGETS" \
  -v "$WORK/src:/src:ro" -v "$ROOT/third_party/openparrot:/third_party:ro" -v "$OUT:/out" \
  "$IMAGE" sh /third_party/build-in-container.sh

# --- 5. vastleggen -----------------------------------------------------------------------------------------------
{
  echo "OpenParrot $OPENPARROT_REPO @ $OPENPARROT_COMMIT (licentie GPL-3.0)"
  echo "gebouwd: $(date -u +%Y-%m-%dT%H:%M:%SZ), doelen: $TARGETS"
  echo "patches:"; (cd "$ROOT/third_party/openparrot" && sha256sum patches/*.patch mingw-compat.h build-in-container.sh compile-one.sh shim.sh win64init.S openparrot32.def | sed 's/^/  /')
  echo "container: $IMAGE"; toolchain_versions | sed 's/^/  /'
  echo "uitvoer:"; (cd "$OUT" && find . -type f \( -name '*.exe' -o -name '*.dll' \) -print0 | sort -z | xargs -0 sha256sum | sed 's/^/  /')
} > "$OUT/BUILDINFO.txt"
# De standaardmap die launch-direct.py voor [Loader] Build = ravengun leest: <...>/ravengun/openparrot/current -> <commit>
if [[ "$(basename "$OUT")" == "$OPENPARROT_COMMIT" ]]; then
  ln -sfn "$OPENPARROT_COMMIT" "$(dirname "$OUT")/current"
fi
echo "klaar: $OUT"
cat "$OUT/BUILDINFO.txt"
