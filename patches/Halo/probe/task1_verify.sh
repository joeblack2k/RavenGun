#!/bin/bash
# Verify the recipe change and the emulator coupling — without starting the game.
set -u
R=~/projects/ravengun
D=~/projects/ravengun-patches/patches/Halo/dropin
DATA=~/.local/share/ravengun
TMP=$(mktemp -d)

echo "############ 1. the recipe is valid JSON and the fields are what we think"
python3 - <<'PY'
import json
d = json.load(open("/home/deck/projects/ravengun/packaging/native-games.json"))
h = d["games"]["Halo"]
for k in ("preload", "exe", "cwd", "route"):
    print("  %-10s %s" % (k, h.get(k)))
print("  libdirs  %s" % [(x["src"], x["dst"]) for x in h.get("libdirs", [])])
print("  libs     %s" % h.get("libs"))
print("  env      %s" % h.get("env"))
print("  extra_ro %s" % h.get("extra_ro"))
PY

echo
echo "############ 2. play-native.sh --dry-run (parses the recipe, resolves every path, starts nothing)"
( cd "$R" && scripts/play-native.sh --game Halo --dry-run ) 2>&1 | tr ' ' '\n' | grep -E '^--(ro-bind|bind)|bwrap|/rg/|LD_PRELOAD|RAVENGUN|game$|^--$' | head -50
echo "exitcode ${PIPESTATUS[0]}"

echo
echo "############ 3. the emulator coupling the hook performs (hh_laad_emulator + hh_koppel_emulator)"
cc -Wall -O2 -o "$TMP/coupling" "$D/../probe/hook-emulator-coupling.c" -ldl 2>&1 | head -5
echo "build exitcode $?"
"$TMP/coupling" "$DATA/hasp/ravengun-hasp-x64.so"
echo "coupling exitcode $?"

echo
echo "############ 4. and with the emulator ALSO in LD_PRELOAD, does it leak into the global namespace?"
env LD_PRELOAD="$DATA/hasp/ravengun-hasp-x64.so" "$TMP/coupling" "$DATA/hasp/ravengun-hasp-x64.so" \
  | grep -E 'globale|GESLAAGD|MISLUKT'

echo "== done"
