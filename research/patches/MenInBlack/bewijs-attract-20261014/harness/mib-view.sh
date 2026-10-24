#!/bin/bash
# The view of Men in Black: ~/.local/share/ravengun/runtime/views/MenInBlack = the cabinet's C:\Sega (the Wine prefix
# ravengun-test-mib has drive_c/Sega pointing here). Everything is a link to the share, EXCEPT the shell folder, which is a
# local folder of links so that two files in it can be local: Shell.exe (see mib-shell-copy.sh) and Game.ini (the shell reads
# its switches from the Game.ini in its working directory). What the shell writes (its logs, teknoparrot.ini) stays local.
# usage: mib-view.sh        (safe to repeat; never writes to the share)
set -eu
NAS="/home/deck/mnt/spidernas_emulation/Teknoparrot/Men in Black"
V="$HOME/.local/share/ravengun/runtime/views/MenInBlack"
mkdir -p "$V"
for d in MIB Launcher ShellData GameData backup; do [ -L "$V/$d" ] || ln -s "$NAS/$d" "$V/$d"; done
if [ -L "$V/shell" ]; then rm "$V/shell"; fi
mkdir -p "$V/shell"
( cd "$NAS/shell" && ls -A ) | while IFS= read -r f; do
  case "$f" in
    Shell.exe|Game.ini) ;;                                   # local files, made below / by mib-shell-copy.sh
    *.log|teknoparrot.ini) ;;                                # the share's old logs are not carried into the view
    *) [ -e "$V/shell/$f" ] || [ -L "$V/shell/$f" ] || ln -s "$NAS/shell/$f" "$V/shell/$f" ;;
  esac
done
[ -e "$V/shell/Game.ini" ] || tr -d '\r' < "$NAS/shell/Game.ini" | sed 's/$/\r/' > "$V/shell/Game.ini"   # byte-for-byte text, CRLF kept
[ -e "$V/shell/Shell.exe" ] || ln -s "$NAS/shell/Shell.exe" "$V/shell/Shell.exe"                        # unmodified until mib-shell-copy.sh runs
echo "view: $V"; ls -la "$V/shell" | grep -v -- '-> /home/deck/mnt' | cut -c1-110
echo "links to the share in shell/: $(find "$V/shell" -maxdepth 1 -type l | wc -l)"
