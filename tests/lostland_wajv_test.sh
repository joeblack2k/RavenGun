#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Lost Land Adventure zonder TeknoParrot (tools/rg-lostland): de afbeelding van het gedeelde geheugen naar het WAJV-invoerblok, native.
# Geen spel, geen Wine. Skip (77) zonder C-compiler.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="${CC:-cc}"; command -v "$CC" >/dev/null || { echo "overgeslagen: geen C-compiler"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
"$CC" -O1 -Wall -Wextra -I"$ROOT/tools/rg-lostland" -o "$T/wajv" "$ROOT/tests/lostland_wajv_map_test.c" "$ROOT/tools/rg-lostland/rg_wajv_map.c" 2>"$T/err" \
  || { cat "$T/err"; echo "FAIL: bouwt niet"; exit 1; }
"$T/wajv" || exit 1
echo "lostland_wajv_test: ok"
