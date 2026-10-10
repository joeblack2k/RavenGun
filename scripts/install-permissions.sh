#!/usr/bin/env bash
# Rechten voor RavenGun in één keer, met één beheerdersprompt (de app start dit via pkexec, zie de eerste-gebruik-wizard):
#   1. de udev-regels (scripts/udev-ravengun.sh --apply): virtuele lightguns en toetsenbord, /dev/uhid
#   2. de Bluetooth-hulp met zijn sudoers-regel (scripts/install-bt-helper.sh --apply): koppelen van Moves zonder wachtwoord
#
#   pkexec /pad/naar/scripts/install-permissions.sh --apply    (zoals de app het doet; vaste argumenten)
#   sudo   scripts/install-permissions.sh --apply              (met de hand)
#   scripts/install-permissions.sh --check                     (zonder root: is alles al in orde? exit 0 = ja)
#   pkexec /pad/naar/scripts/install-permissions.sh --rollback (alles weer weghalen: udev-regels, Bluetooth-hulp, sudoers; zie uninstall-user.sh)
#
# Welke gebruiker: onder pkexec PKEXEC_UID (door pkexec gezet), onder sudo SUDO_USER. Geen argumenten van de gebruiker en
# geen shell-tekst: alleen --apply, --rollback of --check. De twee scripts liggen naast dit script (dezelfde map), dus in de
# geïnstalleerde kopie (<prefix>/share/ravengun/scripts) of in de repo; de udev-regels komen uit packaging/udev ernaast.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
MODE="${1:-}"
[[ $# -le 1 ]] || { echo "Usage: $0 --apply | --rollback | --check" >&2; exit 2; }

case "$MODE" in
  --check)
    rc=0
    echo "== udev rules"; bash "$DIR/udev-ravengun.sh" --check || rc=1
    echo "== Bluetooth helper"; bash "$DIR/install-bt-helper.sh" --check || rc=1
    exit $rc ;;
  --apply|--rollback) ;;
  *) echo "Usage: $0 --apply | --rollback | --check" >&2; exit 2 ;;
esac

[[ $EUID -eq 0 ]] || { echo "Run with pkexec or sudo (administrator rights needed)." >&2; exit 3; }
USER_NAME=""
if [[ -n "${PKEXEC_UID:-}" ]]; then
  [[ $PKEXEC_UID =~ ^[0-9]+$ ]] || { echo "Invalid PKEXEC_UID." >&2; exit 2; }
  USER_NAME=$(id -nu "$PKEXEC_UID" 2>/dev/null || true)
elif [[ -n "${SUDO_USER:-}" ]]; then
  USER_NAME="$SUDO_USER"
fi
[[ $USER_NAME =~ ^[a-z_][a-z0-9_-]{0,31}$ && $USER_NAME != root ]] || { echo "Run from the normal user account (no PKEXEC_UID or SUDO_USER)." >&2; exit 2; }

if [[ $MODE == --rollback ]]; then
  echo "== Removing the Bluetooth helper and the sudoers rule"
  SUDO_USER="$USER_NAME" bash "$DIR/install-bt-helper.sh" --rollback
  echo "== Removing the udev rules"
  bash "$DIR/udev-ravengun.sh" --rollback
  echo "Done."
  exit 0
fi

echo "== Installing the udev rules"
bash "$DIR/udev-ravengun.sh" --apply
echo "== Installing the Bluetooth helper for $USER_NAME"
SUDO_USER="$USER_NAME" bash "$DIR/install-bt-helper.sh" --apply
echo "Done."
