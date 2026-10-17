#!/usr/bin/env bash
# rg-kaboom (tools/rg-kaboom/): de eigen KaboomAdrioFxPlusFourFeeders64.dll. Bouwt de rekenkern met de eenheidstest
# (tests/kaboom_unit.c) op Linux en draait hem: de tabel lezen en afkeuren, het voorbeeld is de ingebouwde standaard, en van het
# bordbeeld naar paneel, spelersknoppen en muntentellers. Ook de meegeleverde tabellen (patches/*/dropin/rg-kaboom.ini) moeten
# geldig zijn. De DLL zelf bouwt tools/dev/build-kaboom.sh (podman, mingw). Skip (77) zonder C-compiler.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="${CC:-cc}"; command -v "$CC" >/dev/null || { echo "overgeslagen: geen C-compiler"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
"$CC" -O1 -g -std=c11 -Wall -Wextra -I"$ROOT/tools/rg-kaboom" -o "$T/kaboom_unit" \
      "$ROOT/tests/kaboom_unit.c" "$ROOT/tools/rg-kaboom/rg_kaboom_kern.c" 2>"$T/cc.err" \
  || { cat "$T/cc.err"; echo "FAIL: de eenheidstest bouwt niet"; exit 1; }
[[ ! -s "$T/cc.err" ]] || { cat "$T/cc.err"; echo "FAIL: waarschuwingen bij het bouwen van de test"; exit 1; }
tabellen=()
for f in "$ROOT"/patches/*/dropin/rg-kaboom.ini; do [[ -f $f ]] && tabellen+=("$f"); done
"$T/kaboom_unit" "$ROOT/tools/rg-kaboom/rg-kaboom.voorbeeld.ini" "${tabellen[@]}" || exit 1
echo "meegeleverde tabellen: ${#tabellen[@]}"
