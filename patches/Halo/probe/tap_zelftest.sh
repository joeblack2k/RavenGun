#!/bin/bash
# De aangescherpte tap: bouwen en bewijzen tegen een GESIMULEERDE apparaatlijst.
# Geen bord, geen spel: alleen het leespatroon van libusb op een nagemaakte sysfs-boom.
set -u
P=~/projects/ravengun-patches/patches/Halo
TMP=$(mktemp -d)
BOOM="$TMP/sys/bus/usb/devices"

echo "############ 1. de tap bouwen"
gcc -O2 -Wall -Wextra -fPIC -shared -o "$TMP/libusb-tap.so" "$P/probe/libusb-tap.c" -ldl 2>&1 | head -10
echo "  gcc exitcode ${PIPESTATUS[0]}"
[ -f "$TMP/libusb-tap.so" ] || { echo "  *** tap bouwen mislukt"; exit 1; }

echo
echo "############ 2. een gesimuleerde apparaatlijst maken (twee apparaten, één ervan 0403:6001)"
mkdir -p "$BOOM/1-1" "$BOOM/2-1"
printf '0403\n' > "$BOOM/1-1/idVendor";   printf '6001\n' > "$BOOM/1-1/idProduct"
printf '1\n'    > "$BOOM/1-1/busnum";     printf '4\n'    > "$BOOM/1-1/devnum"
printf '00\n'   > "$BOOM/1-1/bDeviceClass"; printf '12\n' > "$BOOM/1-1/speed"
python3 -c "open('$BOOM/1-1/descriptors','wb').write(bytes([0x12,0x01,0x00,0x02,0,0,0,0x40,0x03,0x03,0x60,0x01,0x00,0x01]))"
printf '046d\n' > "$BOOM/2-1/idVendor";   printf 'c52b\n' > "$BOOM/2-1/idProduct"
printf '1\n'    > "$BOOM/2-1/busnum";     printf '5\n'    > "$BOOM/2-1/devnum"
printf '00\n'   > "$BOOM/2-1/bDeviceClass"; printf '480\n' > "$BOOM/2-1/speed"
python3 -c "open('$BOOM/2-1/descriptors','wb').write(bytes([0x12,0x01,0x00,0x02,0,0,0,0x40,0x6d,0x04,0x2b,0xc5,0x00,0x01]))"
find "$BOOM" -type f | sort | sed 's/^/  /'

echo
echo "############ 3. de simulatie draaien MET de tap"
gcc -O2 -Wall -o "$TMP/enum_sim" "$P/probe/enum_sim.c" 2>&1 | head -5
echo "  gcc exitcode ${PIPESTATUS[0]}"
rm -f "$TMP/tap.log"
LD_PRELOAD="$TMP/libusb-tap.so" RG_USB_LOG="$TMP/tap.log" RG_USB_SYSFS="$BOOM/" RG_USB_RAW=1 \
  "$TMP/enum_sim" "$BOOM" > "$TMP/sim.out" 2>&1
echo "  exitcode $?"
echo "  --- het tap-logboek ($(wc -l < "$TMP/tap.log" 2>/dev/null || echo 0) regels):"
cat "$TMP/tap.log" 2>/dev/null | sed 's/^/    /'

echo
echo "############ 4. de uitslag: ziet de tap de opsomming nu WEL?"
for s in 'tap start' 'attr idVendor' 'attr idProduct' 'attr busnum' 'APPARAAT' 'DIT IS HET FTDI-BORD'; do
  printf '  %-26s %s\n' "$s" "$(grep -c "$s" "$TMP/tap.log" 2>/dev/null || echo 0)"
done
echo "  --- en de twee gevonden apparaten:"
grep '^.*APPARAAT' "$TMP/tap.log" 2>/dev/null | sed 's/^/    /'

echo
echo "############ 5. en de controle: ZONDER tap levert hetzelfde programma geen logboek"
rm -f "$TMP/geen.log"
RG_USB_LOG="$TMP/geen.log" RG_USB_SYSFS="$BOOM/" "$TMP/enum_sim" "$BOOM" > /dev/null 2>&1
printf '  regels in het logboek zonder tap: %s\n' "$(wc -l < "$TMP/geen.log" 2>/dev/null || echo 0)"
echo "== done ($TMP)"
