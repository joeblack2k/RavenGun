#!/bin/bash
# Read-only static measurement of the Halo FTDI / gun-board path + loader questions.
# No game is started; nothing is written to the NAS.
set -u
G=/home/deck/mnt/spidernas_emulation/Teknoparrot/"Halo Fireteam"/g7/halo
R=~/projects/ravengun
T=$(mktemp -d)
cp "$G/game" "$T/game" 2>/dev/null

echo "############ 1. is the FTDI board / any USB gun hardware attached right now?"
lsusb 2>/dev/null | head -30 || echo "  lsusb MISSING"
echo "--- FTDI 0403: by-id"
ls -l /dev/serial/by-id 2>/dev/null | head
ls -l /dev/ttyUSB* 2>/dev/null || echo "  no /dev/ttyUSB*"
echo "--- usbmon available?"
ls -l /sys/kernel/debug/usb/usbmon 2>/dev/null || echo "  usbmon not exposed"
command -v tshark dumpcap wireshark 2>/dev/null || echo "  no tshark/dumpcap"
echo "--- usb devices with the FTDI vendor id 0403"
grep -l . /sys/bus/usb/devices/*/idVendor 2>/dev/null | while read -r f; do
  v=$(cat "$f"); p=$(cat "${f%idVendor}idProduct")
  [ "$v" = "0403" ] && echo "  0403:$p  $(cat "${f%idVendor}product" 2>/dev/null)"
done

echo
echo "############ 2. the game's interpreter and entry point"
readelf -l "$G/game" 2>/dev/null | sed -n '1,40p'
echo "--- PT_INTERP string:"
python3 - "$G/game" <<'PY'
import sys, struct
d = open(sys.argv[1], 'rb').read()
e_phoff = struct.unpack_from('<Q', d, 32)[0]
e_phentsize, e_phnum = struct.unpack_from('<HH', d, 54)
for i in range(e_phnum):
    off = e_phoff + i * e_phentsize
    t = struct.unpack_from('<I', d, off)[0]
    o, v, pa, fs = struct.unpack_from('<QQQQ', d, off + 8)[:4]
    if t == 3:
        print("   PT_INTERP file 0x%x: %r" % (o, d[o:o+fs]))
PY
echo "--- does the file contain 0xdeadbeef (the ElfLdr2 PLT-stub marker)?"
grep -c $'\xef\xbe\xad\xde' "$G/game" 2>/dev/null || true
python3 - "$G/game" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read()
for pat, what in ((b'\xef\xbe\xad\xde', '0xdeadbeef'), (b'\xef\xbe\xad\xde\xad\xde', '0xdeaddead')):
    print("   %s x%d" % (what, d.count(pat)))
PY
echo "--- first 32 bytes at the entry point 0x414600 (file 0x14600):"
xxd -s 0x14600 -l 64 "$T/game"
echo "--- disassembly of the entry point:"
objdump -D -b binary -m i386:x86-64 -M intel --adjust-vma=0x400000 \
  --start-address=0x414600 --stop-address=0x414680 "$T/game" 2>/dev/null | tail -30

echo
echo "############ 3. FTDI call sites: VID / PID / description / baud"
for r in 0x9de180:0x9de2c0 0x9d0a80:0x9d0b60 0x9d2100:0x9d21c0 \
         0x9d3b40:0x9d3c00 0x9d9e00:0x9d9ec0 0x9da500:0x9da5c0; do
  a=${r%%:*}; b=${r##*:}
  echo "=== region file $a..$b"
  objdump -D -b binary -m i386:x86-64 -M intel --adjust-vma=0x400000 \
    --start-address=$a --stop-address=$b "$T/game" 2>/dev/null | grep -E 'mov +e?(ax|bx|cx|dx|si|di|r8d|r9d),|call|lea|ftdi' | head -40
done

echo
echo "############ 4. baud-rate constants in the image"
python3 - "$T/game" <<'PY'
import sys, struct
d = open(sys.argv[1], 'rb').read()
# encode mov r32, imm32 for the usual registers
for baud, name in ((115200, '115200'), (230400, '230400'), (921600, '921600'),
                   (57600, '57600'), (38400, '38400'), (9600, '9600'),
                   (460800, '460800'), (3000000, '3000000'), (12000000, '12000000')):
    pat = struct.pack('<I', baud)
    hits = []
    for opc in range(0xb8, 0xc0):
        p = bytes([opc]) + pat
        i = d.find(p)
        while i >= 0 and len(hits) < 8:
            hits.append(hex(i)); i = d.find(p, i + 1)
    for pre in (b'\x41', b'\x48'):
        for opc in range(0xb8, 0xc0):
            p = pre + bytes([opc]) + pat
            i = d.find(p)
            while i >= 0 and len(hits) < 12:
                hits.append(hex(i)); i = d.find(p, i + 1)
    if hits:
        print("   baud %-9s x%d  %s" % (name, len(hits), hits))
PY

echo
echo "############ 5. what the game says about the gun board (message catalogue)"
strings -a "$G/game" | grep -nE 'cmgr|ir_Fprog|irgun|ir_UpdateGun|CRC|SAMPLE|led message|status message|switch message|trig|Boggy|fpga|FPGA' | head -80

echo
echo "############ 6. the HASP/dongle path as it stands in this game"
echo "--- hasp-related strings in game:"
strings -a "$G/game" | grep -iE 'hasp|hardlock|sentinel' | head -40
echo "--- does the game resolve hasp_* by name from its own table?"
python3 - "$T/game" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read()
for n in (b'hasp_login', b'hasp_read', b'hasp_write', b'hasp_get_info', b'hasphl'):
    print("   %-14s x%d" % (n.decode(), d.count(n)))
PY
echo "--- the project's own hasp layer (what would replace halo.so):"
ls -l "$R/hasp/src" 2>/dev/null
echo "--- hasp-layer doc?"
ls -l "$R/docs/hasp-layer.md" 2>/dev/null && head -60 "$R/docs/hasp-layer.md"
echo "--- hasphook: which functions does it hook?"
grep -nE 'hasp_|inline_hook|off|addr|0x[0-9a-f]{5,}' "$R/tools/rt-hasphook/hasp_inline_hook.c" 2>/dev/null | head -50

echo
echo "############ 7. native Halo logs: what the game printed (no start)"
for f in $(ls -t ~/.local/state/ravengun/logs/native-Halo-* 2>/dev/null | head -3); do
  echo "=== $f"
  grep -aiE 'cmgr|hasp|ftdi|fmod|dollar|credit|insert|start|test|webcam|calib|gun|RIO2' "$f" | head -30
done
ls -l ~/.local/state/ravengun/logs/native-Halo-* 2>/dev/null | tail -5

echo
echo "== done ($T)"
