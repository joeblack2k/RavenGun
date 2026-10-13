#!/bin/bash
# Wie vraagt WELKE vakken op? De drie aanroepers van de analoog-getter (0x61ee70) en de tien van de
# schakelaar-getter (0x61e890) uitdisassembleren. Bench-vrij, alleen lezen.
set -u
G=/tmp/io-brug-game.bin
echo "############ A. DE PER-BEELD-INVOERVULLER 0x624120 (aanroepen analoog-getter op 0x6241c4 en 0x6241d1)"
objdump -D -b binary -m i386:x86-64 --adjust-vma=0x400000 \
  --start-address=0x624120 --stop-address=0x624260 "$G" 2>&1 | tail -n +8

echo
echo "############ B. DE KLEINE FUNCTIE ROND 0x4ad7c0 (aanroept op 0x4ad800 de ANALOOG-getter en op 0x4ad80c de SCHAKELAAR-getter)"
objdump -D -b binary -m i386:x86-64 --adjust-vma=0x400000 \
  --start-address=0x4ad790 --stop-address=0x4ad860 "$G" 2>&1 | tail -n +8

echo
echo "############ C. DE SCHAKELAAR-LEZER 0x49eb40 (tien aanroepen van de schakelaar-getter in dit blok)"
objdump -D -b binary -m i386:x86-64 --adjust-vma=0x400000 \
  --start-address=0x49eb40 --stop-address=0x49f060 "$G" 2>&1 | tail -n +8
