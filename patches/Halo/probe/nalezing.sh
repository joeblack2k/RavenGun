#!/bin/bash
# Nalezing op de NOG LEVENDE run: de tafelkaart (sectie C/D) met het gerepareerde unpack_from.
# Dit is een LEZING, geen ingreep; de run van 14:37:31 loopt nog.
P=/home/deck/projects/ravengun-patches/patches/Halo/probe
U=/tmp/run20-nalezing
mkdir -p "$U"
python3 -m py_compile "$P/memlees.py" && echo "memlees compileert"
python3 "$P/memlees.py" auto "$U" "nalezing" 2>&1
echo "=== en de pols op bord 0 vak 0 (0x1815d20), 8 s ==="
python3 "$P/pols.py" auto 0x1815d20 8 25 2>&1
