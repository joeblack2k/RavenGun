#!/bin/bash
# Verify the pushed probes run on the Deck, and run the two zero-run checks from the report.
# No game is started; the NAS is only read.
set -u
P=~/projects/ravengun-patches/patches/Halo/probe
G="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo"
STUB=~/.local/share/ravengun/runtime/lib-fmodstub

echo "############ probe: elf_probe.py"
python3 "$P/elf_probe.py" "$G" game 2>&1 | sed -n '1,30p'

echo
echo "############ probe: ftdi_targets.py"
python3 "$P/ftdi_targets.py" "$G" 2>&1

echo
echo "############ probe: fmod_diff.py (NAS game vs the repo stub list)"
python3 "$P/fmod_diff.py" "$G" ~/projects/ravengun/tools/fmod-stub/symbols.txt 2>&1 | head -8

echo
echo "############ probe: extract_pairs.py"
python3 "$P/extract_pairs.py" "$G/game" 0x1b492c8 0x1b54000 2>&1 | head -8

echo
echo "############ CHECK A: is the DT_NEEDED closure the only thing missing?"
LD_LIBRARY_PATH="$STUB" ldd "$G/game" 2>&1 | grep -iE 'not found|=>' | head -20
echo "--- 'not found' lines:"
LD_LIBRARY_PATH="$STUB" ldd "$G/game" 2>&1 | grep -c 'not found'

echo
echo "############ CHECK B: DT_NEEDED count and which ones the stub supplies"
readelf -d "$G/game" 2>/dev/null | grep NEEDED | wc -l
ls -l "$STUB"

echo
echo "############ CHECK C input: the 0xdeadbeef thunk region is present and not BIND_NOW"
python3 "$P/final_checks.py" "$G" 2>&1 | sed -n '1,12p'

echo "== done"
