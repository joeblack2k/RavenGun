#!/usr/bin/env bash
# rg-unis (tools/rg-unis/): de eigen UsbPluginsDll.dll van UNIS (Elevator Action Invasion, Raccoon Rampage). Bouwt de rekenkern met de
# eenheidstest (tests/unis_unit.c) op Linux en draait hem: de tabel lezen en afkeuren, muntflanken, en van het bordbeeld naar de bytes
# van FCoUnisIoData (Raccoon, 49), FESIoData (EAI, 16) en FPACKET (EAI, 92). Ook de meegeleverde tabellen
# (docs/research/wartran/patches/*/dropin/rg-unis.ini) en het voorbeeld moeten geldig zijn. De DLL zelf bouwt tools/dev/build-unis.sh
# (podman, mingw). Skip (77) zonder C-compiler.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="${CC:-cc}"; command -v "$CC" >/dev/null || { echo "overgeslagen: geen C-compiler"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
"$CC" -O1 -g -std=c11 -Wall -Wextra -I"$ROOT/tools/rg-unis" -o "$T/unis_unit" \
      "$ROOT/tests/unis_unit.c" "$ROOT/tools/rg-unis/rg_unis_kern.c" 2>"$T/cc.err" \
  || { cat "$T/cc.err"; echo "FAIL: de eenheidstest bouwt niet"; exit 1; }
[[ ! -s "$T/cc.err" ]] || { cat "$T/cc.err"; echo "FAIL: waarschuwingen bij het bouwen van de test"; exit 1; }
tabellen=()
for f in "$ROOT"/docs/research/wartran/patches/*/dropin/rg-unis.ini "$ROOT"/tools/rg-unis/rg-unis.voorbeeld.ini; do [[ -f $f ]] && tabellen+=("$f"); done
"$T/unis_unit" "${tabellen[@]}" || exit 1
echo "meegeleverde tabellen: ${#tabellen[@]}"
