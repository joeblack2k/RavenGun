#!/bin/sh
# Controleert de JavaScript-syntax met node in een wegwerp-container (rootless podman, geen mounts).
# De container-image blijft ongewijzigd (--rm); nodejs wordt per run geinstalleerd.
# gebruik: jscheck.sh <bestand.js>
set -eu
exec podman run --rm -i localhost/ravengun-build sh -c '
  pacman -Sy --noconfirm nodejs >/dev/null 2>&1
  cat > /tmp/a.js
  node --check /tmp/a.js && echo SYNTAX_OK
' < "$1"
