#!/bin/sh
# fetch.sh <pkgdir> <pool-path>...   e.g.  main/g/glibc/libc6_2.23-0ubuntu3_i386.deb
# Downloads a public Ubuntu .deb from $MIRROR/pool/ (default archive.ubuntu.com/ubuntu; old-releases.ubuntu.com/ubuntu for EOL series) and unpacks its data part into
# <pkgdir>/<deb-name-without-.deb>/ (nothing is executed). launchpadlibrarian.net (older point releases)
# was not reachable from this host on 2026-10-15. Research tool, not delivery.
D=$1; shift
for p in "$@"; do
  f=${p##*/}; n=${f%.deb}; [ -d "$D/$n" ] && { echo "have $f"; continue; }
  mkdir -p "$D/dl"
  curl -sfL -m 600 -o "$D/dl/$f" "${MIRROR:-http://archive.ubuntu.com/ubuntu}/pool/$p" || { echo "FAIL $p"; rm -f "$D/dl/$f"; continue; }
  mkdir -p "$D/$n"
  ( cd "$D/$n" && ar x "../dl/$f" && for t in data.tar.*; do tar xf "$t"; done && rm -f data.tar.* control.tar.* debian-binary ) || echo "UNPACK-FAIL $f"
  echo "ok $f"
done
