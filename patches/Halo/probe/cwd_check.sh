#!/bin/bash
# De laatste onbekende: zet het recept voor Halo een werkmap (cwd) en een home?
set -u
R=~/projects/ravengun
python3 - "$R/packaging/native-games.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
def loop(o, pad=""):
    if isinstance(o, dict):
        naam = " ".join(str(o.get(k, "")) for k in ("name", "dir", "exe", "game", "profile"))
        if "alo" in naam:
            print("  --- gevonden blok (pad %s):" % pad)
            for k, v in o.items():
                if k in ("name", "dir", "dirs", "exe", "args", "cwd", "home", "preload", "libs", "env",
                         "extra_ro", "bind_ro", "bind_rw", "status", "route", "loader"):
                    print("    %-10s %s" % (k, json.dumps(v)[:220]))
        for k, v in o.items():
            loop(v, pad + "." + k)
    elif isinstance(o, list):
        for i, v in enumerate(o):
            loop(v, pad + "[%d]" % i)
loop(d)
PY
echo
echo "############ hoe play-native.sh cwd en home gebruikt"
grep -nE 'cwd|home|chdir|clearenv|setenv' "$R/scripts/play-native.sh" 2>/dev/null | head -14 | sed 's/^/  /'
echo "== done"
