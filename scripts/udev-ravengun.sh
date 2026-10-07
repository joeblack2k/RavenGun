#!/usr/bin/env bash
# Installeert/verwijdert de smalle RavenGun-udev-regel. Gebruik: sudo scripts/udev-ravengun.sh --apply|--rollback|--dry-run
set -euo pipefail
SRC="$(cd "$(dirname "$0")/.." && pwd)/packaging/udev/70-ravengun.rules"; DST=/etc/udev/rules.d/70-ravengun.rules
[[ $EUID -eq 0 ]] || { echo "Start met sudo." >&2; exit 1; }
case "${1:---dry-run}" in
  --dry-run) echo "Zou $SRC installeren als $DST en udev herladen."; [[ -e $DST ]] && echo "(staat er al)";;
  --apply) install -m 644 "$SRC" "$DST"; udevadm control --reload; udevadm trigger --subsystem-match=input --action=change; echo "Geïnstalleerd: $DST";;
  --rollback) rm -f "$DST"; udevadm control --reload; udevadm trigger --subsystem-match=input --action=change; echo "Verwijderd: $DST";;
  *) echo "Gebruik: $0 --dry-run|--apply|--rollback" >&2; exit 2;;
esac
