#!/usr/bin/env bash
# De invoerhulp (tools/rt-input-shim/rg_rt_input.c): schakelaars, geweerknoppen en assen uit het bordbeeld naar het geheugen van
# de game, volgens een tabel per game. Bouwt de eenheidstest (tests/rt_input_unit.c, met de bron van de hulp geinclude) en de
# bibliotheek zelf in een tijdelijke map, en draait de test. Er is geen spel nodig: het "geheugen van de game" is een eigen stuk.
# Ook: het voorbeeld van de tabel (tools/rt-input-shim/rt-invoer.voorbeeld.ini) moet zelf een geldige tabel zijn.
# Skip (77) zonder C-compiler.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="${CC:-cc}"; command -v "$CC" >/dev/null || { echo "overgeslagen: geen C-compiler"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
BRON="$ROOT/tools/rt-input-shim/rg_rt_input.c"
# De rekenregel van de assen is LETTERLIJK die van de IO-brug (eerst afronden, dan inverteren, dan klemmen). De eenheidstest
# vergelijkt de hulp met een overgeschreven kopie van die regel; deze drie regels moeten daarom letterlijk, en in deze orde, in
# de bron van de brug staan. Verandert de brug zijn regel, dan gaat deze test rood en moeten de hulp en de kopie mee.
BRUG="$ROOT/src/ravengun/iobridge/tp_shm.cpp"
vorige=0
for regel in 'double v = std::round(a.min + f * (a.max - a.min));' 'if (a.invert) v = a.min + a.max - v;' \
             'if (!out) v = std::min(255.0 - p.offscreen_margin, std::max(static_cast<double>(p.offscreen_margin), v));'; do
  nr=$(grep -nF -- "$regel" "$BRUG" 2>/dev/null | head -1 | cut -d: -f1)
  [[ -n "$nr" && "$(grep -cF -- "$regel" "$BRUG")" -eq 1 ]] || { echo "FAIL: de rekenregel van de IO-brug is veranderd: '$regel' staat niet (precies een keer) in $BRUG; de hulp en tests/rt_input_unit.c moeten mee"; exit 1; }
  (( nr > vorige )) || { echo "FAIL: de orde van de rekenregel in $BRUG is veranderd (afronden, inverteren, klemmen)"; exit 1; }
  vorige=$nr
done
VB="$ROOT/tools/rt-input-shim/rt-invoer.voorbeeld.ini"
"$CC" -O1 -g -Wall -Wno-unused-function -DRG_RT_INPUT_TEST -DRG_BRON="\"$BRON\"" ${VB:+-DRG_VOORBEELD="\"$VB\""} \
      -o "$T/rt_input_unit" "$ROOT/tests/rt_input_unit.c" -lm -lpthread -ldl 2>"$T/cc.err" \
  || { cat "$T/cc.err"; echo "FAIL: de eenheidstest bouwt niet"; exit 1; }
[[ ! -s "$T/cc.err" ]] || { cat "$T/cc.err"; echo "FAIL: waarschuwingen bij het bouwen van de test"; exit 1; }
# de bibliotheek zelf moet ook bouwen, zoals CMake hem bouwt
"$CC" -shared -fPIC -O2 -w -fno-stack-protector -o "$T/librg_rt_input.so" "$BRON" -lm -lpthread -ldl 2>"$T/so.err" \
  || { cat "$T/so.err"; echo "FAIL: de bibliotheek bouwt niet"; exit 1; }
"$T/rt_input_unit" "$T" || exit 1
