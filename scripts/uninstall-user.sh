#!/usr/bin/env bash
# Verwijdert RavenGun voor de gebruiker: de gebruikersservices, snelkoppelingen, koppelingen in ~/.local/bin en de installatiemap
# (standaard ~/.local/opt/ravengun). De root-onderdelen (udev-regels, Bluetooth-hulp, sudoers-regel) gaan via
# `pkexec <prefix>/share/ravengun/scripts/install-permissions.sh --rollback` (een beheerdersprompt; $RAVENGUN_PKEXEC vervangt
# pkexec, tests). Instellingen en bibliotheek blijven standaard staan; de TeknoParrot-runtime, Wine en andere gegevens onder
# ~/.local/share/ravengun worden NOOIT automatisch verwijderd. De uitvoer is voor de gebruiker en dus Engels.
# $RAVENGUN_SYSTEMCTL vervangt systemctl (tests).
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: uninstall-user.sh [options]

Removes RavenGun: the user services, shortcuts and the installation folder. Your games, the TeknoParrot runtime and
other data in ~/.local/share/ravengun are never removed.

  --prefix DIR        installation folder (default: the one this script runs from, else ~/.local/opt/ravengun)
  --keep-config       keep your settings and library (~/.config/ravengun, ~/.local/state/ravengun) (default)
  --remove-config     also remove your settings, library, controller calibration and logs
  --keep-root         do not remove the system parts (device rules, Bluetooth helper, sudoers rule)
  -y, --yes           do not ask questions (keeps the settings unless --remove-config is given)
  -h, --help          this text
EOF
}

main() {
  local SELF ROOT PREFIX SYSTEMCTL PKEXEC CONFIG_CHOICE="" KEEP_ROOT=0 YES=0
  SELF="$(cd "$(dirname "$0")" && pwd)"
  ROOT="$(cd "$SELF/.." && pwd)"
  SYSTEMCTL="${RAVENGUN_SYSTEMCTL:-systemctl}"
  PKEXEC="${RAVENGUN_PKEXEC:-pkexec}"
  PREFIX="${RAVENGUN_PREFIX:-$HOME/.local/opt/ravengun}"
  # Vanuit een geïnstalleerde kopie: die installatie.
  if [[ "$(basename "$(dirname "$ROOT")")" == share && -x "$ROOT/../../bin/ravengund" ]]; then PREFIX="$(cd "$ROOT/../.." && pwd)"; fi

  while [[ $# -gt 0 ]]; do
    case "$1" in
      --prefix) [[ $# -ge 2 ]] || { echo "Error: --prefix needs a folder" >&2; return 1; }; PREFIX="$2"; shift ;;
      --keep-config) CONFIG_CHOICE=keep ;;
      --remove-config) CONFIG_CHOICE=remove ;;
      --keep-root) KEEP_ROOT=1 ;;
      -y|--yes) YES=1 ;;
      -h|--help) usage; return 0 ;;
      *) usage >&2; echo "Error: unknown option: $1" >&2; return 1 ;;
    esac
    shift
  done
  [[ $EUID -ne 0 ]] || { echo "Error: run this as your normal user, not as root." >&2; return 1; }
  [[ -n ${HOME:-} && -d $HOME ]] || { echo "Error: HOME is not set." >&2; return 1; }

  local DATA="$PREFIX/share/ravengun"
  local SYSDIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
  local APPDIR="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
  local CONFIG="${XDG_CONFIG_HOME:-$HOME/.config}/ravengun"
  local STATE="${XDG_STATE_HOME:-$HOME/.local/state}/ravengun"
  local USERDATA="${XDG_DATA_HOME:-$HOME/.local/share}/ravengun"
  local installed=0
  [[ -f $DATA/scripts/uninstall-user.sh ]] && installed=1
  if [[ $installed -eq 0 ]]; then echo "No RavenGun installation found in $PREFIX (the services and shortcuts are still cleaned up)."; fi

  # --- 1. De gebruikersservices -----------------------------------------------------------------------------------------------
  echo "== Stopping and removing the user services"
  for u in ravengund ravengun-oled-saver; do
    "$SYSTEMCTL" --user disable --now "$u.service" 2>/dev/null || true
    rm -f "$SYSDIR/$u.service"
  done
  rm -rf "$SYSDIR/ravengun-oled-saver.service.d"
  "$SYSTEMCTL" --user daemon-reload 2>/dev/null || true
  # De saver start zijn zwarte venster in een eigen unit; die gaat mee weg.
  "$SYSTEMCTL" --user stop ravengun-oled-view.service 2>/dev/null || true

  # --- 2. Snelkoppelingen en koppelingen --------------------------------------------------------------------------------------
  echo "== Removing shortcuts"
  local f d DESK
  DESK="$HOME/Desktop"
  if command -v xdg-user-dir >/dev/null; then d="$(xdg-user-dir DESKTOP 2>/dev/null || true)"; if [[ -n $d && -d $d && $d != "$HOME" ]]; then DESK="$d"; fi; fi
  for f in "$APPDIR/ravengun-qt.desktop" "$DESK/ravengun-qt.desktop"; do
    # Alleen onze eigen bestanden: ze wijzen naar de installatiemap.
    if [[ -f $f ]] && grep -q "^Exec=$PREFIX/bin/ravengun" "$f" 2>/dev/null; then rm -f "$f"; fi
  done
  if command -v update-desktop-database >/dev/null && [[ -d $APPDIR ]]; then update-desktop-database "$APPDIR" 2>/dev/null || true; fi
  for f in "$HOME/.local/bin/ravengun" "$HOME/.local/bin/ravengunctl"; do
    if [[ -L $f && "$(readlink "$f")" == "$PREFIX/bin/"* ]]; then rm -f "$f"; fi
  done

  # --- 3. De systeemonderdelen (root), nog met de scripts uit de installatiemap -----------------------------------------------
  local have_root=0
  if [[ -e /etc/udev/rules.d/70-ravengun.rules || -e /etc/udev/rules.d/71-ravengun-uhid.rules || -e /opt/ravengun/bin/ravengun-bt-helper ]]; then have_root=1; fi
  if [[ $have_root -eq 1 && $KEEP_ROOT -eq 0 ]]; then
    echo "== Removing the system parts (device rules, Bluetooth helper); you will be asked for your password"
    if [[ $installed -eq 1 ]] && "$PKEXEC" "$DATA/scripts/install-permissions.sh" --rollback; then
      :
    else
      echo "The system parts could not be removed automatically. To remove them yourself, run:" >&2
      echo "  sudo rm -f /etc/udev/rules.d/70-ravengun.rules /etc/udev/rules.d/71-ravengun-uhid.rules /etc/sudoers.d/ravengun-bt-helper" >&2
      echo "  sudo rm -rf /opt/ravengun && sudo udevadm control --reload" >&2
    fi
  elif [[ $have_root -eq 1 ]]; then
    echo "Keeping the system parts (device rules, Bluetooth helper) as requested."
  fi

  # --- 4. De installatiemap ---------------------------------------------------------------------------------------------------
  if [[ $installed -eq 1 ]]; then
    echo "== Removing $PREFIX"
    rm -rf "$PREFIX"
  fi

  # --- 5. Instellingen ---------------------------------------------------------------------------------------------------------
  if [[ -z $CONFIG_CHOICE ]]; then
    CONFIG_CHOICE=keep
    if [[ $YES -eq 0 && -t 0 && ( -d $CONFIG || -d $STATE ) ]]; then
      local ans=""
      read -r -p "Also remove your RavenGun settings, library and logs ($CONFIG, $STATE)? [y/N] " ans || true
      case "$ans" in y|Y|yes|YES) CONFIG_CHOICE=remove ;; esac
    fi
  fi
  if [[ $CONFIG_CHOICE == remove ]]; then
    echo "== Removing your settings and logs"
    rm -rf "$CONFIG" "$STATE"
  else
    echo "Your settings and library are kept in $CONFIG and $STATE."
  fi

  cat <<EOF

RavenGun is removed.
Not removed (remove by hand if you want):
  - $USERDATA: the TeknoParrot runtime, Wine and other data RavenGun downloaded or prepared. Your game folders are untouched.
  - The "RavenGun" entry in your Steam library (Steam > right-click > Manage > Remove non-Steam game).
EOF
}

main "$@"; exit $?
