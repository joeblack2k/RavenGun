#!/bin/bash
# dis.sh VADDR LEN  -- disassemble JP game (i386) from vaddr; annotate call targets with symbol names
G="/home/deck/mnt/spidernas_emulation/Teknoparrot/Jurassic Park/game"
D=$(dirname "$0")
# the symbol list (18,811 names from the game's own dynamic symbol table) is made once, next to this script
[[ -s "$D/jp-syms.txt" ]] || python3 -I "$D/syms.py" "$G" > "$D/jp-syms.txt"
s=$(( $1 )); n=$(( $2 ))
objdump -D -b binary -m i386 --adjust-vma=0x08048000 --start-address=$s --stop-address=$((s+n)) "$G" | tail -n +8 | python3 -I "$D/ann.py" "$D/jp-syms.txt"
