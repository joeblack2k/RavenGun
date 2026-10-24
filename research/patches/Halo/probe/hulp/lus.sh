#!/bin/bash
# Hoe loopt de lus in 0x624120 precies? (index r12, spelerstap, en waar de vlag gezet wordt)
set -u
G=/tmp/io-brug-game.bin
objdump -D -b binary -m i386:x86-64 --adjust-vma=0x400000 --start-address=0x624230 --stop-address=0x624340 "$G" 2>&1 | tail -n +8
