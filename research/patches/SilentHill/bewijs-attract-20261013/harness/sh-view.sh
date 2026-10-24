#!/bin/bash
# Make the parts of the Silent Hill view that the game WRITES local, without copying game content in bulk and without
# touching the share. Idempotent. What becomes local:
#   Data/                  a real directory of links (was one link to the share)
#   Data/TestDat/          local COPY (252 kB: setting.1, gun.1, KSHG.crc) -- the game rewrites setting.1 / gun.1 there
#   Data/acdata/           local COPY (7.8 MB) -- the game deletes/moves files there
#   Data/eng|fra|ita/      real directories of links -- the game makes Data\<lang> its current directory, and our core
#                          redirects E:\ and F:\ (bookkeeping) to .\TeknoParrot\E|F under the current directory
set -eu
NAS="/home/deck/mnt/spidernas_emulation/Teknoparrot/Silent Hill The Arcade"
V="$HOME/.local/share/ravengun/runtime/views/SilentHill"
[ -d "$V" ] || { echo "no view at $V"; exit 1; }
if [ -L "$V/Data" ]; then unlink "$V/Data"; fi
mkdir -p "$V/Data"
for e in "$NAS/Data"/*; do n=$(basename "$e")
  case "$n" in
    TestDat|acdata) [ -d "$V/Data/$n" ] || cp -r --no-preserve=ownership "$e" "$V/Data/$n" ;;
    eng|fra|ita)    mkdir -p "$V/Data/$n"; for f in "$e"/*; do [ -e "$V/Data/$n/$(basename "$f")" ] || ln -s "$f" "$V/Data/$n/$(basename "$f")"; done ;;
    *)              [ -e "$V/Data/$n" ] || ln -s "$e" "$V/Data/$n" ;;
  esac
done
echo "view Data: $(find "$V/Data" -maxdepth 1 -type l | wc -l) links, $(find "$V/Data" -maxdepth 1 -mindepth 1 -type d | wc -l) local directories"
