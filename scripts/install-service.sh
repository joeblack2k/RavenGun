#!/usr/bin/env bash
# Installeert of verwijdert de gebruikersservice ravengund (start bij het inloggen; geen root).
# Gebruik: scripts/install-service.sh --apply | --rollback
set -euo pipefail
MODE="${1:---apply}"
SRC="$(cd "$(dirname "$0")/.." && pwd)/packaging/systemd/ravengund.service"
DST="$HOME/.config/systemd/user/ravengund.service"
case "$MODE" in
  --apply)
    mkdir -p "$(dirname "$DST")" "$HOME/.local/state/ravengun/logs"
    install -m 644 "$SRC" "$DST"
    systemctl --user daemon-reload
    systemctl --user enable --now ravengund.service
    echo "Geïnstalleerd en gestart: $DST" ;;
  --rollback)
    systemctl --user disable --now ravengund.service 2>/dev/null || true
    rm -f "$DST"; systemctl --user daemon-reload
    echo "Verwijderd: $DST" ;;
  *) echo "Gebruik: $0 --apply | --rollback" >&2; exit 2 ;;
esac
