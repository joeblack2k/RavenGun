#!/bin/bash
echo "=== status rg-run20 ==="
systemctl --user status rg-run20.service --no-pager -l 2>&1 | head -25
echo "=== journaal ==="
journalctl --user -u rg-run20.service --no-pager -n 40 2>&1 | tail -40
echo "=== rechten ==="
ls -la /home/deck/run20_wrap.sh /home/deck/projects/ravengun-patches/patches/Halo/probe/run20_uitvoeren.sh
