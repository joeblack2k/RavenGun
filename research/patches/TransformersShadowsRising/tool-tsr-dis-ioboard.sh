#!/usr/bin/env bash
set -euo pipefail
GAME="${1:-/home/deck/mnt/spidernas_emulation/Teknoparrot/Sega/shell/Shell.exe}"
OUT="${2:-/home/deck/tsr_ioboard.asm}"
objdump -d -M intel --start-address=0x439d00 --stop-address=0x43bd00 "$GAME" > "$OUT"
wc -l "$OUT"
