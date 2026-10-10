#!/usr/bin/env bash
# Rechten voor RavenGun in één keer, met één beheerdersprompt (de app start dit via pkexec, zie de eerste-gebruik-wizard):
#   1. de udev-regels (scripts/udev-ravengun.sh --apply): virtuele lightguns en toetsenbord, /dev/uhid
#   2. de Bluetooth-hulp met zijn sudoers-regel (scripts/install-bt-helper.sh --apply): koppelen van Moves zonder wachtwoord
#
#   pkexec /pad/naar/scripts/install-permissions.sh --apply    (zoals de app het doet; vaste argumenten)
#   sudo   scripts/install-permissions.sh --apply              (met de hand)
#   scripts/install-permissions.sh --check                     (zonder root: is alles al in orde? exit 0 = ja)
#
# Welke gebruiker: onder pkexec PKEXEC_UID (door pkexec gezet), onder sudo SUDO_USER. Geen argumenten van de gebruiker en
# geen shell-tekst: alleen --apply of --check. De twee scripts liggen naast dit script (dezelfde map).
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
MODE="${1:-}"
[[ $# -le 1 ]] || { echo "Gebruik: $0 --apply | --check" >&2; exit 2; }

case "$MODE" in
  --check)
    rc=0
    echo "== udev-regels"; bash "$DIR/udev-ravengun.sh" --check || rc=1
    echo "== Bluetooth-hulp"; bash "$DIR/install-bt-helper.sh" --check || rc=1
    exit $rc ;;
  --apply) ;;
  *) echo "Gebruik: $0 --apply | --check" >&2; exit 2 ;;
esac

[[ $EUID -eq 0 ]] || { echo "Start met pkexec of sudo (beheerdersrechten nodig)." >&2; exit 3; }
USER_NAME=""
if [[ -n "${PKEXEC_UID:-}" ]]; then
  [[ $PKEXEC_UID =~ ^[0-9]+$ ]] || { echo "Ongeldige PKEXEC_UID." >&2; exit 2; }
  USER_NAME=$(id -nu "$PKEXEC_UID" 2>/dev/null || true)
elif [[ -n "${SUDO_USER:-}" ]]; then
  USER_NAME="$SUDO_USER"
fi
[[ $USER_NAME =~ ^[a-z_][a-z0-9_-]{0,31}$ && $USER_NAME != root ]] || { echo "Start vanuit het gewone gebruikersaccount (geen PKEXEC_UID of SUDO_USER)." >&2; exit 2; }

echo "== udev-regels installeren"
bash "$DIR/udev-ravengun.sh" --apply
echo "== Bluetooth-hulp installeren voor $USER_NAME"
SUDO_USER="$USER_NAME" bash "$DIR/install-bt-helper.sh" --apply
echo "Klaar."
