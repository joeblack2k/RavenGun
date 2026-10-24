#!/bin/bash
# KAN DE CAMERA IN DE SANDBOX, EN KAN DE DAEMON HEM TEGELIJK OPENHOUDEN? Geen bench nodig.
set -u
R=~/projects/ravengun

echo "############ 1. DE CAMERA BUITEN DE SANDBOX"
ls -l /dev/video* 2>/dev/null | sed 's/^/  /'
for v in /sys/class/video4linux/video*; do [ -e "$v" ] && printf '  %-8s %s\n' "$(basename "$v")" "$(cat "$v/name" 2>/dev/null)"; done
command -v v4l2-ctl >/dev/null && v4l2-ctl --list-devices 2>&1 | head -6 | sed 's/^/  /' || echo "  v4l2-ctl: niet aanwezig"

echo
echo "############ 2. WIE HEEFT HEM NU OPEN? (de daemon streamt hem voor de Move)"
for p in $(pgrep -f 'ravengund|ravengun' 2>/dev/null | head -5); do
  n=$(ls -l /proc/$p/fd 2>/dev/null | grep -c video)
  [ "$n" -gt 0 ] && printf '  pid %-8s %s  -> %s video-fd(s)\n' "$p" "$(cat /proc/$p/comm 2>/dev/null)" "$n"
done
ls -l /proc/*/fd 2>/dev/null | grep -c video0 | sed 's/^/  totaal open video0-fds op de machine: /'

echo
echo "############ 3. DE BIND TESTEN MET BWRAP ZELF (dezelfde koppelingen als play-native)"
python3 - <<'PY' 2>&1 | sed 's/^/  /'
import subprocess, sys
# minimale bwrap die precies doet wat play-native doet voor dit apparaat:
cmd = ["bwrap", "--die-with-parent", "--unshare-pid", "--unshare-net",
       "--ro-bind", "/usr", "/usr", "--symlink", "usr/lib", "/lib", "--symlink", "usr/lib64", "/lib64",
       "--proc", "/proc", "--dev", "/dev",
       "--bind", "/dev/video0", "/dev/video0",          # <-- de koppeling die wij willen
       "--", "python3", "-c",
       "import os,sys;"
       "p='/dev/video0';"
       "print('  in de sandbox: bestaat', os.path.exists(p));"
       "fd=os.open(p, os.O_RDWR);" 
       "print('  open(O_RDWR) gelukt, fd', fd);"
       "os.close(fd);"
       "print('  en de daemon kan hem tegelijk openhouden als dit lukte')"]
r = subprocess.run(cmd, capture_output=True, text=True)
print("  bwrap exitcode:", r.returncode)
print(r.stdout.strip() or "(geen uitvoer)")
if r.stderr.strip():
    print("  stderr:", r.stderr.strip()[:300])
PY

echo
echo "############ 4. WAT PLAY-NATIVE ZELF ZEGT: bestaat er een optie voor een apparaat, en wat wordt het commando?"
grep -nE '^\s*--(bind|dev|device)' "$R/scripts/play-native.sh" | head -8 | sed 's/^/  /'
bash "$R/scripts/play-native.sh" --game Halo --headless --seconds 5 --dry-run \
  --bind-rw /dev/video0 /dev/video0 2>&1 | tr ' ' '\n' | grep -nE 'video0|bwrap|--bind' | head -12 | sed 's/^/  /'

echo
echo "############ 5. STAAT /dev/video0 IN HET RECEPT OF IN EEN ANDERE GAME?"
grep -rn 'video0\|dev/video' "$R/packaging/"*.json "$R/scripts/play-native.sh" 2>/dev/null | head -8 | sed 's/^/  /'
echo "  (leeg = wij geven de camera nergens door)"
echo "== done"
