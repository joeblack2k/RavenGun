#!/usr/bin/env bash
# Installeert/verwijdert de smalle RavenGun-udev-regels: 70 (toetsenbord 1209:7201, lightguns 1209:7202..7205 voor speler 1..4) en
# 71 (schrijfrecht op /dev/uhid voor de gebruiker, alleen nodig voor hid-borden met transport "uhid").
# Gebruik: sudo scripts/udev-ravengun.sh --apply|--rollback|--dry-run
#          scripts/udev-ravengun.sh --check   (zonder sudo: is de geïnstalleerde regel gelijk aan die in de repo?)
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
RULES=(70-ravengun.rules 71-ravengun-uhid.rules)
MODE="${1:---dry-run}"
if [[ $MODE == --check ]]; then
  rc=0
  for R in "${RULES[@]}"; do
    SRC="$REPO/packaging/udev/$R"; DST="/etc/udev/rules.d/$R"
    if [[ ! -e $DST ]]; then echo "Niet geïnstalleerd: $DST"; rc=1; continue; fi
    if cmp -s "$SRC" "$DST"; then echo "Actueel: $DST"; continue; fi
    echo "Verouderd: $DST wijkt af van $SRC (sudo $0 --apply)"; diff -u "$DST" "$SRC" || true; rc=1
  done
  exit $rc
fi
[[ $EUID -eq 0 ]] || { echo "Start met sudo." >&2; exit 1; }
reload() { udevadm control --reload; udevadm trigger --subsystem-match=input --action=change; udevadm trigger --subsystem-match=misc --sysname-match=uhid --action=change; }
case "$MODE" in
  --dry-run)
    for R in "${RULES[@]}"; do
      SRC="$REPO/packaging/udev/$R"; DST="/etc/udev/rules.d/$R"
      echo "Zou $SRC installeren als $DST en udev herladen."
      if [[ -e $DST ]]; then cmp -s "$SRC" "$DST" && echo "(staat er al, gelijk)" || { echo "(staat er al, verschilt:)"; diff -u "$DST" "$SRC" || true; }; fi
    done;;
  --apply) for R in "${RULES[@]}"; do install -m 644 "$REPO/packaging/udev/$R" "/etc/udev/rules.d/$R"; echo "Geïnstalleerd: /etc/udev/rules.d/$R"; done; reload;;
  --rollback) for R in "${RULES[@]}"; do rm -f "/etc/udev/rules.d/$R"; echo "Verwijderd: /etc/udev/rules.d/$R"; done; reload;;
  *) echo "Gebruik: $0 --dry-run|--apply|--rollback|--check" >&2; exit 2;;
esac
