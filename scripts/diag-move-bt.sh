#!/usr/bin/env bash
# Read-only Bluetooth-diagnose voor de PS Move (vereist root voor /var/lib/bluetooth en btmon).
# Wijzigt niets. Schrijft één logbestand naar de state-map van de gebruiker die sudo aanriep.
#
# Gebruik:  sudo ~/projects/ravengun/scripts/diag-move-bt.sh
#           koppel daarna binnen 30 s de USB-kabel los en druk kort op de PS-knop.
set -u
[[ $EUID -eq 0 ]] || { echo "Start met sudo." >&2; exit 1; }
U="${SUDO_USER:-deck}"; H=$(getent passwd "$U" | cut -d: -f6)
OUT="$H/.local/state/ravengun/logs/move-bt-$(date +%Y%m%d-%H%M%S).log"
mkdir -p "$(dirname "$OUT")"

{
  echo "== BlueZ-opslag (mappen per adapter; apparaatmappen met MAC-naam)"
  for a in /var/lib/bluetooth/*/; do
    echo "adapter: $a"
    ls -la "$a" | grep -E ' [0-9A-F]{2}(:[0-9A-F]{2}){5}$|cache' || true
  done
  echo
  echo "== Info-bestanden met Name=Motion Controller (zonder sleutels)"
  for f in /var/lib/bluetooth/*/*/info; do
    if grep -q 'Motion Controller' "$f"; then
      echo "--- $f"; grep -viE 'Key|IRK|LTK' "$f"
    fi
  done
  echo
  echo "== input.conf (actieve regels)"
  grep -vE '^\s*(#|$)' /etc/bluetooth/input.conf || echo "(geen actieve regels)"
  echo
  echo "== btmon 30 s — koppel nu de USB-kabel los en druk op de PS-knop"
} | tee "$OUT"

timeout 30 btmon -T >>"$OUT" 2>&1
chown "$U": "$OUT"
echo
echo "Klaar. Log: $OUT"
