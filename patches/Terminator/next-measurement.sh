#!/bin/bash
# next-measurement.sh - the exact next measurement for Terminator Salvation, as a runnable script.
#
# Deliberately STATIC: it runs no guest code and cannot bring up a window on the bench.
#
# It builds the dongle / HASP-HL symbol map for the STRIPPED `game` from the unstripped, DWARF-bearing
# ServiceDongleApp that ships in the same folder. Two tools, five evidence methods:
#
#   find-dongle-symbols.py     [BYTES]   code-block match (mismatch ratio over non-address bytes)
#                              [STRING]  byte-identical block around a referenced string
#   find-dongle-adjacency.py   [ADJACENCY] span between two proven neighbours identical in both builds
#                              [ADJACENCY-SPAN] the same, with a corroborated split
#                              [ALIAS]    rel32 target of a tail-jump == target found independently
#                              [EXACT-BODY PATTERN + ADJACENCY] unique occurrence by neighbour distance
#
# EXPECTED RESULT (measured 2026-10-12/13): 50 of 58 located with evidence, 8 unresolved.
#   part 1: 43   (grade A 24 byte-identical, B 12 at <=3%, C 7 at <=6%)
#   part 2:  7   (DongleGetID, hasp_cleanup, hasp_enable_trace, hasp_free, hasp_get_trace,
#                 hasp_hasptime_to_datetime, hasp_login_ex)
#   unresolved 8: AcquireAladdinDongle, DongleFeatureCheck, DongleInitialize, GetAladdinHLPass,
#                 GetAladdinSRMPass, ReInitAladdinDongles, ReleaseAladdinDongle, SysGetDongleHID
#   each unresolved one carries a measured reason (its containing region changed size between builds).
#
# Run on the Deck:
#     cd ~/projects/ravengun-patches/patches/Terminator
#     bash next-measurement.sh
# READ-ONLY on the NAS. Starts nothing.
set -u

GAME="/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation /game"
SDA="/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation /ServiceDongleApp/ServiceDongleApp"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

for f in "$GAME" "$SDA"; do
  [ -r "$f" ] || { echo "MISSING/UNREADABLE: $f"; exit 1; }
done

echo "=== 0. sanity: the guest must still be the UNPACKED build ==="
readelf -h "$GAME" | grep -E 'Class|Type:|Entry|Number of section headers'
printf '  HASPE marker occurrences : %s (must be 0)\n' "$(strings -a "$GAME" | grep -c HASPE)"
printf '  DT_HASH present          : %s (must be 0 -> this is the loader blocker, README 2.2)\n' \
  "$(readelf -d "$GAME" | grep -cE '\(HASH\)')"
printf '  DT_GNU_HASH present      : %s (must be 1)\n' \
  "$(readelf -d "$GAME" | grep -cE '\(GNU_HASH\)')"
echo

echo "=== 1. part 1: code-block matching ==="
python3 "$HERE/find-dongle-symbols.py" | grep -E 'located with evidence|grade |UNKNOWN  \('
echo

echo "=== 2. part 2: adjacency-span + alias ==="
python3 "$HERE/find-dongle-adjacency.py" | grep -E 'part 1|part 2|total located|still unresolved'
echo

echo "=== 3. the 8 that remain, with their measured reason ==="
python3 "$HERE/find-dongle-adjacency.py" | grep -A10 'UNRESOLVED'
echo
echo "SUCCESS CRITERION: 50/58 located with evidence, and 8 unresolved each carrying a measured reason."
echo "That is the state of 2026-10-12/13. Finishing the last 8 needs a NEW independent anchor (the"
echo "game-side prologue walk from the leads in README section 4, or a dynamic trace of 'main' during the"
echo "bench run - see DRAAIBOEK-BENCH.md section 5), NOT a looser match threshold."
