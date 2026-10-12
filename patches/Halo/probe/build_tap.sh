#!/bin/bash
# Build and smoke-test the USB tap. No game is started; nothing touches the NAS.
set -u
OUT=~/projects/ravengun-patches/patches/Halo/probe
mkdir -p "$OUT"

echo "=== headers present?"
ls -l /usr/include/linux/usbdevice_fs.h /usr/include/linux/usb/ch9.h 2>&1

echo
echo "=== building the tap"
gcc -shared -fPIC -O2 -Wall -Wextra -o "$OUT/libusb-tap.so" "$OUT/libusb-tap.c" -ldl 2>&1
RC=$?
echo "gcc exitcode $RC"
[ -f "$OUT/libusb-tap.so" ] && ls -l "$OUT/libusb-tap.so"

echo
echo "=== smoke test A: does it load and stay passive under an unrelated program?"
rm -f /tmp/usb-tap-smoke.log
RG_USB_LOG=/tmp/usb-tap-smoke.log LD_PRELOAD="$OUT/libusb-tap.so" /bin/ls /dev >/dev/null 2>&1
echo "ls exitcode $?"
echo "log lines: $(wc -l < /tmp/usb-tap-smoke.log 2>/dev/null || echo 'no log')"
cat /tmp/usb-tap-smoke.log 2>/dev/null | head -5

echo
echo "=== smoke test B: does it log a real usbfs open?"
rm -f /tmp/usb-tap-smoke2.log
RG_USB_LOG=/tmp/usb-tap-smoke2.log RG_USB_LOG_STDOUT=1 \
  LD_PRELOAD="$OUT/libusb-tap.so" /bin/ls -l /dev/bus/usb/003/003 2>&1 | head -3
echo "--- log:"
head -10 /tmp/usb-tap-smoke2.log 2>/dev/null || echo "(no log; nobody opened /dev/bus/usb)"

echo
echo "=== smoke test C: undefined symbols of the tap (must be only libc/libdl)"
nm -D --undefined-only "$OUT/libusb-tap.so" 2>/dev/null | awk '{print $NF}' | sort -u | head -30

echo
echo "=== what the FTDI arguments in the game resolve to (static)"
echo "   ftdi_usb_find_all entry point: vaddr 0xFF9CD0 (six call sites, all 0x0403:0x6001)"
echo "== done"
