#!/bin/bash
# Makes a local copy of Jurassic Park's `game` whose RIO_Connected reports the board CONNECTED. The dump is
# only read; the share is never written. usage: jp-game-rio-patch.sh [out]  (default ~/.local/share/ravengun/rio/JurassicPark/game)
# THE CHANGE (RIO_Connected, vaddr 0x0836b320 = file offset 0x323320):
#   55 89 e5 ..   push ebp; mov ebp,esp; mov eax,[0x8e7d61c]; ... -> 0 if connected, -3 if not
#   becomes  31 c0 c3   xor eax,eax; ret   (always 0 = connected)
# RESULT (measured): the "InputInit(): RIO not connected (-3)" line is gone, but the game then enters full-cabinet
# init and stalls in a hasp_decrypt(792) loop, rendering black. So this clears JP's RIO wall but reveals the DONGLE
# as the next wall -- it does NOT yet give a banner-gone attract frame. See patches/RIO-BORD.md.
set -eu
SRC="/home/deck/mnt/spidernas_emulation/Teknoparrot/Jurassic Park/game"
OUT="${1:-$HOME/.local/share/ravengun/rio/JurassicPark/game}"; mkdir -p "$(dirname "$OUT")"
python3 -I - "$SRC" "$OUT" <<'PY'
import sys,hashlib
src,out=sys.argv[1],sys.argv[2]; b=bytearray(open(src,'rb').read())
want='c18a9329e72a3639...'  # prefix check only
foff=0x836b320-0x08048000
if bytes(b[foff:foff+3])!=bytes.fromhex('5589e5'): sys.exit('REFUSED: RIO_Connected not where expected (another version?)')
b[foff:foff+3]=bytes.fromhex('31c0c3'); open(out,'wb').write(b)
print('patched game -> %s  sha256 %s'%(out,hashlib.sha256(b).hexdigest()))
PY
