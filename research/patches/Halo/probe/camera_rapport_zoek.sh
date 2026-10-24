#!/bin/bash
# ZOEK HET RAPPORT VAN DE LADERBOUWER OVER DE CAMERA (en lees het relevante deel).
set -u
R=~/projects/ravengun
PP=~/projects/ravengun-patches

echo "############ 1. RECENTE DOSSIERS OVER DE CAMERA (waar dan ook in de twee bomen)"
find "$R" "$PP" -name '*.md' -newermt '2026-10-13 11:00' 2>/dev/null | head -30 | sed 's/^/  /'

echo
echo "############ 2. WIE NOEMT DE CAMERA-SHIM, V4L2 OF 'presence'?"
grep -rliE 'v4l2|reqbufs|video0|camera.*shim|shim.*camera|Use *= *presence|presence.*rol' \
  "$R/docs" "$R/packaging" "$PP/patches" 2>/dev/null | head -12 | sed 's/^/  /'

echo
echo "############ 3. DE KERN VAN ZIJN METING (de regels eromheen)"
for f in $(grep -rliE 'reqbufs|v4l2shim|v4l2_shim|Use *= *presence' "$R/docs" "$PP/patches" 2>/dev/null | head -4); do
  echo "  === $f"
  grep -inE 'reqbufs|device or resource busy|EBUSY|tweede|second|30 fps|elke tweede|shared memory|gedeeld geheugen|mmap|ioctl|readdir|__xstat|presence|synthetisch|synthetic|per speler|one camera|twee camera' "$f" | head -22 | sed 's/^/    /'
done

echo
echo "############ 4. DE HUIDIGE HULP: bron, artefact en of mijn regelsoorten er nog in zitten"
stat -c '  bron : %s bytes, %y' "$R/tools/rt-input-shim/rg_rt_input.c" 2>/dev/null
stat -c '  .so  : %s bytes, %y' "$R/build/librg_rt_input.so" 2>/dev/null
sha256sum "$R/build/librg_rt_input.so" 2>/dev/null | sed 's/^/  sha256 /'
strings -n 10 "$R/build/librg_rt_input.so" 2>/dev/null | grep -cE 'te veel assen|te veel tellers|\(zet\)' | sed 's/^/  mijn regelsoorten in het artefact: /'
echo "  --- en wijst het ijkpunt nog naar dezelfde bron? (git)"
cd "$R" && git log --oneline -3 2>/dev/null | sed 's/^/    /'
git status --porcelain tools/rt-input-shim/ 2>/dev/null | sed 's/^/    ongecommitte wijziging: /'
echo "== done"
