#!/usr/bin/env bash
# Bouwt de FMOD-wrapper (tools/fmod-bridge): libfmod.so.10 en libfmodstudio.so.10 (Linux, in het spel) uit fmod_bridge.c, en
# rg-fmod-host.exe (Windows x86_64, onder Wine) uit rg_fmod_host.c met de mingw-kruiscompiler in ~/.cache/ravengun-mingw.
# Er zit geen FMOD-code in; de echte FMOD komt van de gebruiker (een native drop-in, of de FMOD Studio 1.10.2 van een
# TeknoParrot-installatie). De twee .so-bestanden bouwt cmake ook (doelen rg_fmod10 en rg_fmodstudio10, in build/fmod).
#   tools/dev/build-fmod-bridge.sh [--lib MAP] [--exe MAP]
#     --lib MAP  standaard build/fmod in de repo (de libdir @build/fmod van patches/Halo/native.json)
#     --exe MAP  standard patches/Halo/data/fmod in this repo (the delivery per game)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/tools/fmod-bridge"
LIB="$ROOT/build/fmod"
EXE="$ROOT/patches/Halo/data/fmod"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --lib) LIB="$2"; shift 2 ;;
    --exe) EXE="$2"; shift 2 ;;
    -h|--help) sed -n '2,8p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
done
mkdir -p "$LIB" "$EXE"
for so in libfmod.so.10 libfmodstudio.so.10; do
  cc -O2 -shared -fPIC -fvisibility=hidden -Wall -Wextra -Werror -Wno-pedantic -Wl,-soname,"$so" "$SRC/fmod_bridge.c" -o "$LIB/$so.tmp" -lpthread -ldl -lm
  mv "$LIB/$so.tmp" "$LIB/$so"
done
# Elke naam uit symbols.txt moet geëxporteerd zijn (de game zoekt ze zelf op met dlsym).
mist=$(comm -23 <(sort -u "$SRC/symbols.txt") <(nm -D --defined-only "$LIB/libfmod.so.10" | awk '{print $3}' | sort -u))
[[ -z "$mist" ]] || { echo "ontbreekt in de wrapper:" >&2; echo "$mist" >&2; exit 1; }
MINGW="${RAVENGUN_MINGW:-$HOME/.cache/ravengun-mingw}"
SR="$MINGW/sysroot"; ARCH=x86_64
CC="$SR/usr/bin/$ARCH-w64-mingw32-gcc"
[[ -x "$CC" ]] || { echo "geen $ARCH-mingw in $MINGW (zie $MINGW/BUILD.md)" >&2; exit 1; }
GCCV="$(ls "$SR/usr/lib/gcc/$ARCH-w64-mingw32/" | sort -V | tail -1)"
"$CC" -B"$SR/usr/lib/gcc/$ARCH-w64-mingw32/$GCCV/" -B"$SR/usr/$ARCH-w64-mingw32/" -O2 -s -Wall -Wextra -Werror -Wno-cast-function-type \
  -static -static-libgcc -Wl,--no-insert-timestamp "$SRC/rg_fmod_host.c" -o "$EXE/rg-fmod-host.exe.tmp"
mv "$EXE/rg-fmod-host.exe.tmp" "$EXE/rg-fmod-host.exe"
echo "klaar: $LIB/libfmod.so.10, libfmodstudio.so.10 ($(sha256sum "$LIB/libfmod.so.10" | cut -c1-16)); $EXE/rg-fmod-host.exe ($(sha256sum "$EXE/rg-fmod-host.exe" | cut -c1-16))"
