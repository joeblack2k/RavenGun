#!/bin/bash
# Revert my own write to the shared recipe (only my hunks are in it) and show the clean base.
set -u
cd ~/projects/ravengun

echo "=== before"
git status --porcelain packaging/native-games.json
git diff --stat -- packaging/native-games.json

git checkout -- packaging/native-games.json
echo "reverted; exitcode $?"

echo "=== after"
git status --porcelain packaging/native-games.json && echo "(leeg = schoon)"
git diff --stat -- packaging/native-games.json && echo "(leeg = schoon)"

echo "=== the Halo block in the restored (committed) file"
python3 - <<'PY'
import json
h = json.load(open("packaging/native-games.json"))["games"]["Halo"]
print("  preload  :", h["preload"])
print("  libdirs  :", [x["src"] + " -> " + x["dst"] for x in h["libdirs"]])
print("  libs     :", h["libs"])
print("  env      :", h.get("env"))
print("  extra_ro :", h["extra_ro"])
print("  enabled  :", h["enabled"], "| status:", h["status"])
PY

echo "=== baseline commit of that file"
git log --oneline -1 -- packaging/native-games.json
echo "== done"
