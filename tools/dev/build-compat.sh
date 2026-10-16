#!/usr/bin/env bash
# Bouwt rg-compat.dll (tools/rg-compat/rg_compat.c): kleine Wine-compatibiliteitsfixes in het spelproces (GetCharABCWidthsA met
# een buffer van een ABC, IDirectInput::Initialize met een lege HINSTANCE). Met de mingw-kruiscompiler in ~/.cache/ravengun-mingw.
#   tools/dev/build-compat.sh [--arch i686|x86_64] --out MAP [--out MAP]...      standaard i686; MAP is meestal patches/<Game>/dropin
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/tools/rg-compat/rg_compat.c"
MINGW="${RAVENGUN_MINGW:-$HOME/.cache/ravengun-mingw}"
ARCH=i686
OUTS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --arch) ARCH="$2"; shift 2 ;;
    --out) OUTS+=("$2"); shift 2 ;;
    -h|--help) sed -n '2,4p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
done
[[ ${#OUTS[@]} -gt 0 ]] || { echo "geef --out MAP (bv. patches/<Game>/dropin)" >&2; exit 2; }
SR="$MINGW/sysroot"
CC="$SR/usr/bin/$ARCH-w64-mingw32-gcc"
[[ -x "$CC" ]] || { echo "geen $ARCH-mingw in $MINGW (zie $MINGW/BUILD.md)" >&2; exit 1; }
GCCV="$(ls "$SR/usr/lib/gcc/$ARCH-w64-mingw32/" | sort -V | tail -1)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
"$CC" -B"$SR/usr/lib/gcc/$ARCH-w64-mingw32/$GCCV/" -B"$SR/usr/$ARCH-w64-mingw32/" \
  -O2 -s -shared -Wall -Wextra -Werror -static-libgcc -Wl,--no-insert-timestamp -Wl,--enable-stdcall-fixup \
  "$SRC" -o "$TMP/rg-compat.dll"
for o in "${OUTS[@]}"; do
  mkdir -p "$o"
  cp "$TMP/rg-compat.dll" "$o/rg-compat.dll"
  echo "klaar: $o/rg-compat.dll"
done
echo "bron: $(sha256sum "$SRC" | cut -c1-16) $(basename "$SRC"); dll ($ARCH): $(sha256sum "$TMP/rg-compat.dll" | cut -c1-16)"
