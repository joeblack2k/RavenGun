#!/bin/bash
echo "=== $(date '+%T')  WAAROM STIERF RUN 20? ==="
echo "--- de tijdelijke uitvoermap:"
ls -dt /tmp/run20-* 2>/dev/null | head -3
O=$(ls -dt /tmp/run20-* 2>/dev/null | head -1)
echo "   laatste = $O"
[ -n "$O" ] && { echo "   inhoud:"; ls -la "$O"; echo "   play.out staart:"; tail -25 "$O/play.out" 2>/dev/null; }
echo
echo "--- de spel-crash in het gamelogboek (context):"
G=/home/deck/.local/share/ravengun/native/Halo/0/upper/pmuser/gamelog.txt
wc -l "$G"
tail -60 "$G"
echo
echo "--- de laatste launcherlogs:"
ls -t /home/deck/.local/state/ravengun/logs/native-Halo-*.log | head -3
echo
echo "--- wat zegt het systeemjournaal (laatste 25 van deze gebruiker)?"
journalctl --user -n 25 --no-pager 2>&1 | tail -25
echo
echo "--- OOM of segfault in de kernelring?"
dmesg 2>/dev/null | tail -15 || echo "   (dmesg niet leesbaar zonder rechten)"
