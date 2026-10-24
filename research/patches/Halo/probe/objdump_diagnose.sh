#!/bin/bash
# Waarom gaf objdump geen uitvoer op de spelbinary? Diagnose.
B="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo/game"
echo "--- bestaat het bestand en hoe groot:"
ls -l "$B"
echo "--- file:"
file "$B" 2>&1 | head -2
echo "--- readelf -h (Type/Machine/Entry):"
readelf -h "$B" 2>&1 | grep -iE 'Type:|Machine:|Entry' | sed 's/^/  /'
echo "--- program headers (LOAD):"
readelf -lW "$B" 2>&1 | grep -E '^  LOAD' | head -5 | sed 's/^/  /'
echo "--- objdump op een klein bereik, MET stderr:"
objdump -D --start-address=0x61e880 --stop-address=0x61e900 "$B" > /tmp/od.out 2> /tmp/od.err
echo "    exitcode: $?"
echo "    stdout-regels: $(wc -l < /tmp/od.out)   stderr-regels: $(wc -l < /tmp/od.err)"
echo "    --- stderr (eerste 4):"; head -4 /tmp/od.err | sed 's/^/      /'
echo "    --- stdout (eerste 8):"; head -8 /tmp/od.out | sed 's/^/      /'
echo "--- en op een adres dat zeker code is (entrypunt uit de kop):"
ENT=$(readelf -h "$B" 2>/dev/null | awk '/Entry point/{print $4}')
echo "    entry: $ENT"
objdump -D --start-address=$((ENT)) --stop-address=$((ENT+32)) "$B" 2>/tmp/od2.err | head -6 | sed 's/^/      /'
echo "    stderr: $(head -2 /tmp/od2.err | tr '\n' ' ')"
echo "--- alternatieven aanwezig?"
for p in ndisasm objdump gdb python3; do command -v $p >/dev/null && echo "    $p: ja" || echo "    $p: nee"; done
python3 -c "import capstone; print('    capstone: ja, versie', capstone.__version__)" 2>/dev/null || echo "    capstone: nee"
