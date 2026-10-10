#!/usr/bin/env bash
# Installeert of verwijdert de gebruikersservice ravengund (start bij het inloggen; geen root).
# Gebruik: scripts/install-service.sh --apply [--no-start] | --rollback
#   --apply     het sjabloon packaging/systemd/ravengund.service.in invullen met de map van ravengund (installatie:
#               <prefix>/bin, repo: <repo>/build), naar ~/.config/systemd/user/ schrijven, activeren en starten
#   --no-start  alleen schrijven en activeren (niet starten; bv. voor tests en een installatie in een tijdelijke map)
# $RAVENGUN_SYSTEMCTL vervangt systemctl (tests). De uitvoer van de unit gaat naar ~/.local/state/ravengun/logs/ravengund.log.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/scripts/rg-paths.sh"
SYSTEMCTL="${RAVENGUN_SYSTEMCTL:-systemctl}"
MODE="${1:---apply}"
NOSTART=0; [[ "${2:-}" == --no-start ]] && NOSTART=1
SRC="$ROOT/packaging/systemd/ravengund.service.in"
UNITDIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
DST="$UNITDIR/ravengund.service"
STATE="${XDG_STATE_HOME:-$HOME/.local/state}/ravengun"
case "$MODE" in
  --apply)
    [[ -f $SRC ]] || { echo "Template missing: $SRC" >&2; exit 1; }
    [[ -x $RG_BIN/ravengund ]] || { echo "ravengund not found in $RG_BIN (build it first, or install RavenGun)." >&2; exit 1; }
    case "$RG_BIN$ROOT" in *[[:space:]%\"\'\\]*) echo "The path contains spaces or special characters, which the service does not support: $RG_BIN" >&2; exit 1;; esac
    mkdir -p "$UNITDIR" "$STATE/logs"
    TMP="$(mktemp "$UNITDIR/.ravengund.service.XXXXXX")"
    trap 'rm -f "$TMP"' EXIT
    sed -e "s|@BINDIR@|$RG_BIN|g" -e "s|@DATADIR@|$ROOT|g" "$SRC" > "$TMP"
    chmod 644 "$TMP"; mv -f "$TMP" "$DST"; trap - EXIT
    "$SYSTEMCTL" --user daemon-reload
    if [[ $NOSTART -eq 1 ]]; then "$SYSTEMCTL" --user enable ravengund.service; else "$SYSTEMCTL" --user enable --now ravengund.service; fi
    if [[ $NOSTART -eq 1 ]]; then echo "Installed (not started): $DST"; else echo "Installed and started: $DST"; fi ;;
  --rollback)
    "$SYSTEMCTL" --user disable --now ravengund.service 2>/dev/null || true
    rm -f "$DST"; "$SYSTEMCTL" --user daemon-reload
    echo "Removed: $DST" ;;
  *) echo "Usage: $0 --apply [--no-start] | --rollback" >&2; exit 2 ;;
esac
