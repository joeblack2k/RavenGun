#!/bin/bash
# Two facts before touching any drop-in: are the two patches trees the same, and does
# Terminator have any hasp-ish name at all? Read-only.
set -u
R=~/projects/ravengun
PR=~/projects/ravengun-patches
N=/home/deck/mnt/spidernas_emulation/Teknoparrot

echo "############ 1. is ~/projects/ravengun/patches hetzelfde als ~/projects/ravengun-patches/patches?"
ls -ld "$R/patches" | sed 's/^/  /'
ls -ld "$PR/patches" | sed 's/^/  /'
echo "--- dezelfde Halo-hook in beide bomen?"
for f in "$R/patches/Halo/dropin/libravengun-hasphook-x64.so" "$PR/patches/Halo/dropin/libravengun-hasphook-x64.so"; do
  if [ -f "$f" ]; then printf '  %-70s %s\n' "$f" "$(sha256sum "$f" | cut -c1-16)"; else echo "  ONTBREEKT $f"; fi
done
echo "--- inode-vergelijking (zelfde bestand = zelfde inode):"
stat -c '%i %n' "$R/patches/Halo/dropin/libravengun-hasphook-x64.so" 2>/dev/null | sed 's/^/  /'
stat -c '%i %n' "$PR/patches/Halo/dropin/libravengun-hasphook-x64.so" 2>/dev/null | sed 's/^/  /'
echo "--- waar schrijft build-hasphook.sh heen?  (ROOT=/pad/naar/ravengun)"
grep -n 'ROOT=\|patches/\$g' "$R/scripts/build-hasphook.sh" | head -5 | sed 's/^/  /'

echo
echo "############ 2. Terminator: komt 'hasp' in welke vorm dan ook voor in de dynsym?"
python3 - <<'PY'
import subprocess, sys
sys.path.insert(0, "/home/deck/projects/ravengun-patches/patches/Halo/probe")
import dynsym_probe as dp
for naam, pad in (("Terminator", "/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation /game"),
                  ("JurassicPark", "/home/deck/mnt/spidernas_emulation/Teknoparrot/Jurassic Park/game")):
    r = dp.parse(pad)
    hits = sorted({s[0] for s in r["syms"] if "hasp" in s[0].lower() or "sentinel" in s[0].lower()})
    print("  %-14s symbolen=%d  met hasp/sentinel: %d" % (naam, len(r["syms"]), len(hits)))
    for h in hits[:40]:
        print("      %s" % h)
PY

echo
echo "############ 3. Terminator: heeft hij een dongle-dropin nodig, en vraagt hij erom?"
echo "--- heeft de game een HASP-string?"
strings -a "$N/Terminator Salvation /game" | grep -icE 'hasp|hardlock|sentinel' || true
strings -a "$N/Terminator Salvation /game" | grep -iE 'hasp|hardlock|sentinel' | head -8 | sed 's/^/  /'
echo "--- en Jurassic Park:"
strings -a "$N/Jurassic Park/game" | grep -icE 'hasp|hardlock|sentinel' || true
echo "== done"
