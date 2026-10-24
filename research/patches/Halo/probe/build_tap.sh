#!/bin/bash
# Build and smoke-test the USB tap — without starting any game.
set -u
OUT=~/projects/ravengun-patches/patches/Halo/probe
mkdir -p "$OUT"

echo "=== 1. build"
gcc -shared -fPIC -O2 -Wall -Wextra -o "$OUT/libusb-tap.so" "$OUT/libusb-tap.c" -ldl 2>&1
echo "gcc exitcode $?"
[ -f "$OUT/libusb-tap.so" ] && ls -l "$OUT/libusb-tap.so"

echo
echo "=== 2. passive under an unrelated program, and the start marker is written"
# Let op: interpositie pakt alleen aanroepen die via de PLT van het programma of van een
# bibliotheek gaan. `ls /dev` bereikt `open` via glibc-intern (opendir), dus daar verschijnt
# niets — dat is geen fout van de tap. `head` roept `open` wel via zijn eigen PLT.
rm -f /tmp/tap-a.log
RG_USB_LOG=/tmp/tap-a.log LD_PRELOAD="$OUT/libusb-tap.so" head -c 1 /etc/hostname >/dev/null
echo "head exitcode $?"
echo "log:"; sed 's/^/   /' /tmp/tap-a.log 2>/dev/null || echo "   (no log)"

echo
echo "=== 3. a synthetic sysfs descriptor read, with the FTDI VID/PID in it"
# libusb reads the device list and the descriptors from sysfs on Linux. Give the tap a fake
# sysfs root with one device whose descriptor says 0403:6001 and read it from a normal program.
FAKE=/tmp/fake-sysfs/bus/usb/devices/1-1
mkdir -p "$FAKE"
printf '\x12\x01\x00\x02\x00\x00\x00\x40\x03\x04\x01\x60\x00\x01\x01\x02\x03\x01' > "$FAKE/descriptors"
rm -f /tmp/tap-b.log
RG_USB_LOG=/tmp/tap-b.log RG_USB_SYSFS=/tmp/fake-sysfs/bus/usb/devices/ \
  LD_PRELOAD="$OUT/libusb-tap.so" head -c 18 "$FAKE/descriptors" >/dev/null
echo "head exitcode $?"
echo "log:"; sed 's/^/   /' /tmp/tap-b.log 2>/dev/null || echo "   (no log)"

echo
echo "=== 4. no undefined symbol outside libc/libdl"
nm -D --undefined-only "$OUT/libusb-tap.so" 2>/dev/null | awk '{print $NF}' | sort -u | tr '\n' ' '; echo

echo
echo "=== 5. the glibc floor it needs (it is loaded into the game process)"
objdump -T "$OUT/libusb-tap.so" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -3
echo "== done"
