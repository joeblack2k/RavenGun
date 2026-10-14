#!/bin/bash
# MEASUREMENT ONLY (run jpC1), not a fix. Makes a local copy of Jurassic Park's `game` in which BOTH RIO_Connected (0x836b320)
# and RIO_Connect (0x836b128) return 0 -- the most the NERF table's role "verbonden" (return 0) can say. Input: the
# banner-fixed copy made by ../bewijs-rio-20261014/jp-game-rio-patch.sh. The share is only read.
# usage: jp-game-rio-connect-patch.sh [in] [out]
set -eu
IN="${1:-$HOME/.local/share/ravengun/rio/JurassicPark/game}"; OUT="${2:-$HOME/.local/share/ravengun/rio/JurassicPark/game-armC}"
python3 -I - "$IN" "$OUT" <<'PY'
import sys,hashlib
src,out=sys.argv[1],sys.argv[2]; b=bytearray(open(src,'rb').read())
c=0x836b128-0x08048000; d=0x836b320-0x08048000
if bytes(b[d:d+3])!=bytes.fromhex('31c0c3'): sys.exit('REFUSED: RIO_Connected is not the banner-fixed copy')
if bytes(b[c:c+3])!=bytes.fromhex('5589e5'): sys.exit('REFUSED: RIO_Connect not where expected (another version?)')
b[c:c+3]=bytes.fromhex('31c0c3'); open(out,'wb').write(b)
print('arm-C game -> %s  sha256 %s'%(out,hashlib.sha256(b).hexdigest()))
PY
chmod +x "$OUT"
