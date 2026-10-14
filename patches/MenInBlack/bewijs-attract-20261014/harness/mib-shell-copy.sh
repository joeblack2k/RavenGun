#!/bin/bash
# Makes the view's LOCAL shell/Shell.exe: a copy of the share's Game Shell 230307RED with ONE change of nine bytes, and prints the
# /k= argument that copy needs. The share's file is only read. usage: mib-shell-copy.sh [--original]
#   --original   put the link to the share's unmodified Shell.exe back
#
# THE CHANGE (file offset 0xbc21, address 0x40c821, in the shell's I/O-board check):
#     83 3d dc b8 4b 01 00   cmp dword [0x14bb8dc],0     "has the I/O board been found?"
#     74 19                  je  ...                     not yet -> wait 5 s, then "IO BOARD FAILURE" and stop
#   becomes
#     31 c0 40               xor eax,eax ; inc eax
#     a3 dc b8 4b 01         mov [0x14bb8dc],eax         the flag "I/O board found" is simply set
#     90                     nop
# It is the same change the dump of Transformers: Shadows Rising carries in its own Shell.exe. It does NOT emulate the
# board: the shell believes a board is there and gets no data from it. That is this title's open item, not a solution.
#
# THE /k= ARGUMENT: the shell checks a checksum of the file "shell.exe" in its working directory against /k=<8 hex digits> on
# its command line (on the cabinet SystemLauncher.exe computes and passes it). A changed file needs a changed value; this
# script computes it the way the shell does (table at address 0x55d8a0 of the file itself, start value 0x6f5a87d5).
set -eu
NAS="/home/deck/mnt/spidernas_emulation/Teknoparrot/Men in Black/shell/Shell.exe"
OUT="$HOME/.local/share/ravengun/runtime/views/MenInBlack/shell/Shell.exe"
if [ "${1:-}" = "--original" ]; then rm -f "$OUT"; ln -s "$NAS" "$OUT"; echo "view: link to the share's unmodified Shell.exe"; exit 0; fi
python3 -I - "$NAS" "$OUT.new" <<'PY'
import sys, struct, hashlib
src, out = sys.argv[1], sys.argv[2]
b = bytearray(open(src, 'rb').read())
want = 'e6a7bae54391bb518a529a294462f364fd0abb8de954e9986a38eb5c692ef882'
got = hashlib.sha256(b).hexdigest()
if got != want: sys.exit('REFUSED: the share\'s Shell.exe is another file (sha256 %s)' % got)
T = struct.unpack_from('<256I', b, 0x55d8a0 - 0x559000 + 0x157800)          # the shell's own table, in its .data section
def k(data, c=0x6f5a87d5):
    for x in data: c = ((c << 8) & 0xffffffff) ^ T[((c >> 24) ^ x) & 0xff]
    return c
find, repl = bytes.fromhex('833ddcb84b01007419'), bytes.fromhex('31c040a3dcb84b0190')
if b.count(find) != 1 or b[0xbc21:0xbc2a] != find: sys.exit('REFUSED: the nine bytes are not where they are expected')
print('unmodified Shell.exe: /k=%08x' % k(b))
b[0xbc21:0xbc2a] = repl
open(out, 'wb').write(b)
print('local copy          : /k=%08x   sha256 %s' % (k(b), hashlib.sha256(b).hexdigest()))
PY
rm -f "$OUT"; mv "$OUT.new" "$OUT"; ls -la "$OUT" | cut -c1-120
