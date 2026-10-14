#!/bin/bash
# Makes the view's LOCAL shell/Game.ini from the share's, with the two [Debug] switches the shell reads from the Game.ini in
# its working directory. usage: mib-gameini.sh <SecurityDisabled 0|1> <NoVerify 0|1>
#   SecurityDisabled=1  the shell carries on without the DK2 security key (it then cannot learn the cabinet type from it)
#   NoVerify=1          the shell does not ask for the backup copy of the game's install file and does not checksum the game
# Everything else is the share's file, byte for byte (CRLF line ends kept). The share is never written.
set -eu
NAS="/home/deck/mnt/spidernas_emulation/Teknoparrot/Men in Black/shell/Game.ini"
OUT="$HOME/.local/share/ravengun/runtime/views/MenInBlack/shell/Game.ini"
tr -d '\r' < "$NAS" | awk -v S="$1" -v N="$2" '
  /^\[Debug\]$/ { print; print "SecurityDisabled=" S; indebug=1; next }
  /^\[/ { indebug=0 }
  indebug && /^NoVerify=/ { print "NoVerify=" N; next }
  { print }' | sed 's/$/\r/' > "$OUT.new"
mv "$OUT.new" "$OUT"
echo "local Game.ini written; it differs from the share's in:"; diff <(tr -d '\r' < "$NAS") <(tr -d '\r' < "$OUT") || true
