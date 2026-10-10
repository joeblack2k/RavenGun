#!/usr/bin/env bash
# RavenGun installer (from the release archive). Installs for your user only; no root needed.
#   ./install.sh                 install into ~/.local/opt/ravengun
#   ./install.sh --prefix DIR    install somewhere else
#   ./install.sh --help          all options
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec bash "$HERE/share/ravengun/scripts/install-user.sh" "$@"
