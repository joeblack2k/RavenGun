#!/bin/bash
# next-measurement.sh - the exact next measurement for Terminator Salvation, as a runnable script.
#
# It is deliberately STATIC: it runs no guest code and cannot bring up a window on the bench.
# It locates the dongle / HASP-HL entry points inside the STRIPPED `game` by fingerprinting them
# against the unstripped, DWARF-bearing ServiceDongleApp that ships in the same folder.
#
# Run on the Deck:
#     bash next-measurement.sh
# Success: find-dongle-symbols.py exits 0 and prints "SUCCESS"
#          (DongleInitialize, DongleInit, DongleClose, DongleIsPresent, DongleGetID,
#           AcquireAladdinDongle, GetAladdinHLPass, DongleFeatureCheck, hasp_login, hasp_read
#           all reported with an address in `game`).
# Current status: exit 2 / PARTIAL - 42 of 50 located, 8 still unmatched. See README.md section 4.
#
# READ-ONLY on the NAS. Starts nothing.
set -u

GAME="/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation /game"
SDA="/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation /ServiceDongleApp/ServiceDongleApp"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

for f in "$GAME" "$SDA"; do
  [ -r "$f" ] || { echo "MISSING/UNREADABLE: $f"; exit 1; }
done

echo "=== 1. class sanity check (the game must not have grown section headers) ==="
readelf -h "$GAME" | grep -E 'Class|Type:|Entry|Number of section headers'
printf 'HASPE marker occurrences: %s (must be 0)\n' "$(strings -a "$GAME" | grep -c HASPE)"
printf '0xdeadbeef occurrences:   %s (must be 0)\n' "$(grep -c -a -F -e "$(printf '\357\276\255\336')" "$GAME")"
printf 'DT_HASH present:          %s (must be no -> this is the loader blocker, see README 2.2)\n' \
  "$(readelf -d "$GAME" | grep -cE '\(HASH\)')"
echo

echo "=== 2. the dongle symbol map for the stripped game ==="
python3 "$HERE/find-dongle-symbols.py"
rc=$?
echo
echo "=== 3. what still has to be located (8 functions, see README section 4) ==="
echo "    DongleInitialize, DongleGetID, AcquireAladdinDongle, ReleaseAladdinDongle,"
echo "    GetAladdinHLPass, DongleFeatureCheck, ReInitAladdinDongles, SysGetDongleHID"
echo "    Refinement: WIN 256 -> 512, MAXMISS 6% -> 10%, or match an interior sub-window."
echo "    Already-known handle: the string 'DongleInitialize() FAILED!' (vaddr 0x084feb4e) is loaded"
echo "    at game 0x080c2cdb, which lies inside game's own DongleInitialize."
exit $rc
