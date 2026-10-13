#!/bin/bash
# WAAR PUBLICEERT DE DAEMON? Bench-vrij: geen spel nodig, alleen de daemon en de shm-bestanden.
set -u
RAV=~/projects/ravengun/build/ravengunctl
pid=$(pgrep -x ravengund | head -1)

echo "############ 1. WELKE GEDEELDE-GEHEUGENBESTANDEN ZIJN ER, EN WELKE HOUDT DE DAEMON OPEN?"
ls -l /dev/shm 2>/dev/null | sed 's/^/  /'
echo "  --- de daemon (pid ${pid:-?}):"
ls -l /proc/${pid:-0}/fd 2>/dev/null | grep -iE 'shm|jvs|tekno|ravengun' | sed 's/^/    /'
echo "  --- en zijn mappings:"
grep -iE 'shm|Jvs' /proc/${pid:-0}/maps 2>/dev/null | head -6 | sed 's/^/    /'

echo
echo "############ 2. WAT ER IN DIE BESTANDEN STAAT (bytes 0..19), EN WAT ER VERANDERT ALS IK EEN POSITIE STUUR"
lees_alles() {
  for f in /dev/shm/*; do
    [ -f "$f" ] || continue
    sz=$(stat -c%s "$f" 2>/dev/null)
    [ "$sz" = 64 ] || continue
    python3 -c "
import sys
d=open('$f','rb').read(64)
print('    %-40s b8-11=%s b12-15=%s b16-19=%s' % ('$f', list(d[8:12]), list(d[12:16]), list(d[16:20])))" 2>/dev/null
  done
}
echo "  --- VOOR:"
lees_alles
echo "  --- test-pattern point 0.5 0.5:"
$RAV test-pattern point 0.5 0.5 2>&1 | grep -iE 'Positie|Tellers' | sed 's/^/    /'
sleep 2
echo "  --- NA:"
lees_alles
echo "  --- test-pattern point 0.9 0.1:"
$RAV test-pattern point 0.9 0.1 2>&1 | grep -iE 'Positie' | sed 's/^/    /'
sleep 2
echo "  --- NA:"
lees_alles
echo "  --- en een knop: output-button trigger tap"
$RAV output-button trigger tap 2>&1 | head -2 | sed 's/^/    /'
sleep 2
echo "  --- NA:"
lees_alles

echo
echo "############ 3. WAT DE DAEMON ZEGT OVER ZIJN UITVOER (en of er een IO-brug in zijn config staat)"
$RAV status 2>&1 | grep -iE 'Uitvoer|Tellers|IO|brug|shm|Jvs|muis|speler' | head -12 | sed 's/^/  /'
echo "  --- noemt de configuratie de IO-brug?"
grep -rniE 'jvs|tp-shm|TeknoParrot_JvsState|iobridge|io_brug' ~/.config/ravengun/*.conf 2>/dev/null | head -8 | sed 's/^/  /'
echo "  --- en bestaat het standaardpad dat de hulp als standaard noemt?"
ls -l /dev/shm/TeknoParrot_JvsState 2>/dev/null | sed 's/^/  /' || echo "    /dev/shm/TeknoParrot_JvsState bestaat NIET"
echo "== done"
