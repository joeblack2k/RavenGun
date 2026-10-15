#!/usr/bin/env bash
# Bouwt de stille FMOD (tools/fmod-stub/fmod_stub.c) als libfmod.so.10 en libfmodstudio.so.10 in MAP (standaard
# ~/.local/share/ravengun/runtime/lib-fmodstub, waar packaging/native-games.json hem zoekt). Geen FMOD-code; er klinkt niets.
#   scripts/build-fmod-stub.sh [--out MAP]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${HOME}/.local/share/ravengun/runtime/lib-fmodstub"
[[ "${1:-}" == "--out" ]] && OUT="$2"
mkdir -p "$OUT"
for so in libfmod.so.10 libfmodstudio.so.10; do
  cc -O2 -shared -fPIC -fvisibility=hidden -Wall -Wextra -Werror -Wl,-soname,"$so" "$ROOT/tools/fmod-stub/fmod_stub.c" -o "$OUT/$so.tmp"
  mv "$OUT/$so.tmp" "$OUT/$so"
done
# Elke naam uit symbols.txt moet geëxporteerd zijn (de game zoekt ze zelf op met dlsym).
mist=$(comm -23 <(sort -u "$ROOT/tools/fmod-stub/symbols.txt") <(nm -D --defined-only "$OUT/libfmod.so.10" | awk '{print $3}' | sort -u))
[[ -z "$mist" ]] || { echo "ontbreekt in de stub:" >&2; echo "$mist" >&2; exit 1; }
echo "klaar: $OUT/libfmod.so.10 en libfmodstudio.so.10 ($(sha256sum "$OUT/libfmod.so.10" | cut -c1-16))"
