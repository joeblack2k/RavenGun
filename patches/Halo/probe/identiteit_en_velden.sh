#!/bin/bash
# (a) Staat de IDENTITEITSREGEL van de launcher in mijn logboek?  (b) de exacte velden voor de code.
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
NAT=$(ls -t ~/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | head -1)

echo "############ 1. de identiteitsregel in het launcherlogboek"
echo "  bestand: ${NAT:-geen}"
[ -n "${NAT:-}" ] && { echo "  --- alle regels met sha/launcher/kern/versie:"; grep -inE 'sha256|launcher|play-native\.sh|kern |versie|build' "$NAT" 2>/dev/null | head -12 | sed 's/^/    /'; }
echo "  --- en in de uitvoer van de launcher (play.out):"
grep -inE 'sha256|launcher|play-native\.sh|kern ' /tmp/run11-*/play.out /tmp/run12-*/play.out 2>/dev/null | head -12 | sed 's/^/    /'
echo "  --- wat de launcher zelf over zichzelf zegt (kop van het script):"
grep -nE 'sha256|sha1|identiteit|versie|REV|git' "$R/scripts/play-native.sh" 2>/dev/null | head -8 | sed 's/^/    /'
echo "  --- en wat launch-direct.py erover zegt:"
grep -nE 'sha256|kern \(|core_sha256' "$R/scripts/launch-direct.py" 2>/dev/null | head -8 | sed 's/^/    /'

echo
echo "############ 2. de exacte velden voor de code (uit het profiel)"
python3 - "$R/packaging/tp-shm-profiles.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
p = (d.get("profiles") or d).get("WartranTroopers")
print("  bits (offset 8), alleen speler 1 en de kastschakelaars:")
for b in p["bits"]:
    if b["signal"] in ("test","service","coin1","coin2","volume_up","volume_down") or b["signal"].startswith("p1."):
        print("    %-12s bit %d" % (b["signal"], b["bit"]))
print("  assen:")
for a in p.get("axes", []):
    print("    %-8s offset %s, %s byte(s), min %s max %s, invert %s" %
          (a.get("signal"), a.get("offset"), a.get("bytes"), a.get("min"), a.get("max"), a.get("invert")))
PY

echo
echo "############ 3. hoe de assen in de tp-shm-code geschreven worden (het voorbeeld)"
sed -n '140,170p' "$R/src/ravengun/iobridge/tp_shm.cpp" 2>/dev/null | sed 's/^/  /'
echo "== done"
