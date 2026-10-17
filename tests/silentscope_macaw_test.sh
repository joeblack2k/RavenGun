#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Test van de zuivere logica van de eigen Konami-lader (tools/rg-macaw/rg_macaw_logic.h) en de exportlijst van onze libacio.dll.
# Gebruik: tests/silentscope_macaw_test.sh [<cc>]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
CC="${1:-${CC:-gcc}}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
"$CC" -std=c11 -Wall -Wextra -Werror -O1 -fsanitize=address,undefined -o "$TMP/unit" "$HERE/silentscope_macaw_unit.c"
"$TMP/unit" "$ROOT/tools/rg-macaw/libacio.exports" "$ROOT/tools/rg-macaw/rg_libacio.c"
