#!/bin/bash
# Makes a local librio.so whose RIO_Connected reports the board CONNECTED, so the "RIO DISCONNECTED" banner
# clears. It reads the dump's own librio.so and changes THREE bytes; the share is never written.
# usage: nerf-librio-rio-patch.sh [out.so]   (default: ~/.local/share/ravengun/rio/NERF/librio.so)
#
# THE CHANGE (RIO_Connected, vaddr 0x0000a300 = file offset 0xa300):
#   48 8b 05 ..   mov rax,[rpdata] ; cmpl $1,0x1c(rax) ; sbb/and -> 0 if connected, -3 if not
#   becomes
#   31 c0 c3      xor eax,eax ; ret        (always 0 = connected)
# RIO_Connect still runs the real pmrt_usb_open(0x0c70,0x0780); it fails (no board) and no read thread
# starts, so nothing reads a missing report. The game polls RIO_Connected, sees connected, drops the banner.
# This clears the banner; it does NOT yet deliver inputs (that needs the board's report answered -- see
# patches/RIO-BORD.md). The report format is readable from librio's exported names.
set -eu
SRC="/home/deck/mnt/spidernas_emulation/Teknoparrot/NERF Arcade/Nerf_Data/Plugins/x86_64_ori/librio.so"
OUT="${1:-$HOME/.local/share/ravengun/rio/NERF/librio.so}"; mkdir -p "$(dirname "$OUT")"
python3 -I - "$SRC" "$OUT" <<'PY'
import sys,hashlib
src,out=sys.argv[1],sys.argv[2]; b=bytearray(open(src,'rb').read())
want='a32fb2e61b0e2a6bf281b4e4f7426775b09c66c785a32cffe23eb4fa9fd0b605'
got=hashlib.sha256(b).hexdigest()
if got!=want: sys.exit('REFUSED: the dump librio.so is another file (sha256 %s)'%got)
foff=0xa300
if bytes(b[foff:foff+3])!=bytes.fromhex('488b05'): sys.exit('REFUSED: RIO_Connected not where expected')
b[foff:foff+3]=bytes.fromhex('31c0c3')
open(out,'wb').write(b)
print('patched librio.so -> %s  sha256 %s'%(out,hashlib.sha256(b).hexdigest()))
PY
