#!/usr/bin/env bash
# Installeert/verwijdert de smalle RavenGun-udev-regel (toetsenbord 1209:7201, lightguns 1209:7202..7205 voor speler 1..4).
# Gebruik: sudo scripts/udev-ravengun.sh --apply|--rollback|--dry-run
#          scripts/udev-ravengun.sh --check   (zonder sudo: is de geïnstalleerde regel gelijk aan die in de repo?)
set -euo pipefail
SRC="$(cd "$(dirname "$0")/.." && pwd)/packaging/udev/70-ravengun.rules"; DST=/etc/udev/rules.d/70-ravengun.rules
MODE="${1:---dry-run}"
if [[ $MODE == --check ]]; then
  if [[ ! -e $DST ]]; then echo "Niet geïnstalleerd: $DST"; exit 1; fi
  if cmp -s "$SRC" "$DST"; then echo "Actueel: $DST"; exit 0; fi
  echo "Verouderd: $DST wijkt af van $SRC (sudo $0 --apply)"; diff -u "$DST" "$SRC" || true; exit 1
fi
[[ $EUID -eq 0 ]] || { echo "Start met sudo." >&2; exit 1; }
case "$MODE" in
  --dry-run)
    echo "Zou $SRC installeren als $DST en udev herladen."
    if [[ -e $DST ]]; then cmp -s "$SRC" "$DST" && echo "(staat er al, gelijk)" || { echo "(staat er al, verschilt:)"; diff -u "$DST" "$SRC" || true; }; fi;;
  --apply) install -m 644 "$SRC" "$DST"; udevadm control --reload; udevadm trigger --subsystem-match=input --action=change; echo "Geïnstalleerd: $DST";;
  --rollback) rm -f "$DST"; udevadm control --reload; udevadm trigger --subsystem-match=input --action=change; echo "Verwijderd: $DST";;
  *) echo "Gebruik: $0 --dry-run|--apply|--rollback|--check" >&2; exit 2;;
esac
