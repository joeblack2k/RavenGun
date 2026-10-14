#!/usr/bin/env bash
# Bouwt de Lindbergh-laag voor de native Lindbergh-route (docs/lindbergh.md): lindbergh-loader (lindbergh.so en de vervangende
# libsegaapi/libkswapapi/libposixtime) uit de openbare bron, met de i386-bibliotheken die SteamOS niet heeft, in een rootless
# podman-container (Ubuntu 22.04 i386). Licentie van de bron: CC BY-NC-SA 4.0, NIET verenigbaar met de GPL van RavenGun; daarom
# zit de bron niet in deze repository en bouwt dit script haar op je eigen machine (third_party/lindbergh-loader/README.md).
#
# Gebruik: scripts/build-lindbergh-loader.sh [--out DIR] [--fetch-only] [--versions]
#   --out DIR     uitvoer (standaard ~/.local/share/ravengun/lindbergh-loader/<commit>/; $RAVENGUN_LINDBERGH_OUT), met
#                 daarnaast de koppeling current -> <commit> die de daemon leest
#   --fetch-only  alleen de bronnen ophalen en controleren
#   --versions    commits, basis-image en pakketversies van de buildomgeving tonen
#
# Vastgepind: lindbergh-loader en de afhankelijkheden op commit (hieronder, gecontroleerd na het ophalen), de container (basis-
# image op digest, pakketten via een momentopname van snapshot.ubuntu.com) en onze patches (third_party/lindbergh-loader/patches).
# De gedownloade bronnen gelden als onvertrouwd: alleen `git fetch` en `git archive`; gebouwd wordt uitsluitend in de container,
# zonder netwerk. Er wordt geen game gestart.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

LL_REPO="https://github.com/lindbergh-loader/lindbergh-loader.git"
LL_COMMIT="0af606d845b70339c335785c0eba68b47b78df3c"          # 2026-05-05, master
DEPS=(
  "SDL|https://github.com/libsdl-org/SDL.git|fa2c02bb6e21974a89ea9824bc53c9932abe5f9c"           # release-3.4.16
  "SDL_ttf|https://github.com/libsdl-org/SDL_ttf.git|a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b"   # release-3.2.2
  "SDL_image|https://github.com/libsdl-org/SDL_image.git|f661fa1ad24ab1b81e43662532f9a6a9fcf67ea6" # release-3.4.6
  "FAudio|https://github.com/FNA-XNA/FAudio.git|667942a9097991f5cb6d1532a7ac3142e0581102"        # 26.09
)
CONTAINERFILE="$ROOT/packaging/containers/lindbergh-loader/Containerfile"
SRCROOT="${RAVENGUN_LINDBERGH_SRC:-$HOME/.cache/ravengun/src}"
WORKROOT="${RAVENGUN_LINDBERGH_WORK:-$HOME/.cache/ravengun/build/lindbergh-loader}"
OUT="${RAVENGUN_LINDBERGH_OUT:-${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/lindbergh-loader/$LL_COMMIT}"
FETCH_ONLY=0; VERSIONS=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUT="$2"; shift 2 ;;
    --fetch-only) FETCH_ONLY=1; shift ;;
    --versions) VERSIONS=1; shift ;;
    -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
done

# --- 1. bronnen ophalen (vastgepinde commits) ---------------------------------------------------------------------
fetch_one() {   # naam repo commit
  local dir="$SRCROOT/$1" got
  mkdir -p "$dir"
  if [[ ! -d "$dir/.git" ]]; then
    git -C "$dir" init -q
    git -C "$dir" remote add origin "$2"
  fi
  if ! git -C "$dir" cat-file -e "$3^{commit}" 2>/dev/null; then
    git -C "$dir" fetch -q --depth 1 origin "$3"
  fi
  got="$(git -C "$dir" rev-parse --verify "$3^{commit}")"
  [[ "$got" == "$3" ]] || { echo "bron $1: verkeerde commit $got (verwacht $3)" >&2; exit 1; }
  echo "bron: $1 @ $got"
}
fetch_all() {
  fetch_one lindbergh-loader "$LL_REPO" "$LL_COMMIT"
  local d n r c
  for d in "${DEPS[@]}"; do IFS='|' read -r n r c <<<"$d"; fetch_one "$n" "$r" "$c"; done
}

# --- 2. container -------------------------------------------------------------------------------------------------
IMAGE="localhost/ravengun-lindbergh-build:$(sha256sum "$CONTAINERFILE" | cut -c1-12)"
ensure_image() {
  if ! podman image exists "$IMAGE"; then
    echo "Image $IMAGE bouwen (eerste keer; haalt de pakketten van snapshot.ubuntu.com)..."
    podman build -q -t "$IMAGE" -f "$CONTAINERFILE" "$(dirname "$CONTAINERFILE")" >/dev/null
  fi
}
toolchain_versions() {
  podman run --rm --network=none "$IMAGE" sh -c \
    'dpkg-query -W -f "\${Package}:\${Architecture} \${Version}\n" gcc-11 gcc-multilib cmake libstdc++5:i386 libncurses5:i386 libxmu6:i386 libfreetype6-dev:i386 libpulse-dev:i386 libgl-dev:i386 freeglut3-dev:i386; echo "base-image: $(grep -m1 ^PRETTY_NAME /etc/os-release)"'
}

if [[ $VERSIONS -eq 1 ]]; then
  echo "lindbergh-loader: $LL_REPO @ $LL_COMMIT"
  for d in "${DEPS[@]}"; do IFS='|' read -r n r c <<<"$d"; echo "$n: $r @ $c"; done
  echo "Containerfile: $CONTAINERFILE ($(grep -m1 '^FROM' "$CONTAINERFILE" | cut -d' ' -f2), $(grep -m1 'ARG SNAPSHOT' "$CONTAINERFILE"))"
  ensure_image; toolchain_versions
  exit 0
fi

fetch_all
[[ $FETCH_ONLY -eq 1 ]] && exit 0
ensure_image

# --- 3. bronnen klaarzetten (zonder git-map) ----------------------------------------------------------------------
WORK="$WORKROOT/$LL_COMMIT"
rm -rf "$WORK"; mkdir -p "$WORK/src" "$WORK/work"
export_src() {   # naam commit
  mkdir -p "$WORK/src/$1"
  git -C "$SRCROOT/$1" archive "$2" | tar -x -C "$WORK/src/$1" --no-same-owner --no-same-permissions
}
export_src lindbergh-loader "$LL_COMMIT"
for d in "${DEPS[@]}"; do IFS='|' read -r n r c <<<"$d"; export_src "$n" "$c"; done

# --- 4. bouwen ---------------------------------------------------------------------------------------------------
rm -rf "$OUT"; mkdir -p "$OUT"
podman run --rm --network=none \
  -v "$WORK/src:/src:ro" -v "$WORK/work:/work" -v "$ROOT/third_party/lindbergh-loader/patches:/patches:ro" \
  -v "$ROOT/third_party/lindbergh-loader/build-in-container.sh:/build.sh:ro" -v "$OUT:/out" \
  "$IMAGE" sh /build.sh

# --- 5. vastleggen -----------------------------------------------------------------------------------------------
{
  echo "lindbergh-loader $LL_REPO @ $LL_COMMIT (licentie CC BY-NC-SA 4.0; alleen voor eigen, niet-commercieel gebruik)"
  for d in "${DEPS[@]}"; do IFS='|' read -r n r c <<<"$d"; echo "$n $r @ $c"; done
  echo "gebouwd: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "patches:"; (cd "$ROOT/third_party/lindbergh-loader" && sha256sum patches/*.patch build-in-container.sh | sed 's/^/  /')
  echo "container: $IMAGE"; toolchain_versions | sed 's/^/  /'
  echo "uitvoer:"; (cd "$OUT" && find . -maxdepth 1 -type f ! -name BUILDINFO.txt -print0 | sort -z | xargs -0 sha256sum | sed 's/^/  /')
} > "$OUT/BUILDINFO.txt"
# De standaardmap die de daemon leest (@lindbergh in packaging/native-games.json): <...>/lindbergh-loader/current -> <commit>
if [[ "$(basename "$OUT")" == "$LL_COMMIT" ]]; then
  ln -sfn "$LL_COMMIT" "$(dirname "$OUT")/current"
fi
echo "klaar: $OUT"
cat "$OUT/BUILDINFO.txt"
