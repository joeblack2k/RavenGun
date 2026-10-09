#!/bin/sh
# Draait de JavaScript-unittests van de kalibratie-UI en de app-logica (tests/js/*.js) met node in een wegwerp-container
# (rootless podman, geen mounts; de image blijft ongewijzigd) en controleert de syntaxis van de web-JS.
# Afsluitcode 77 (overgeslagen) als podman of de image ontbreekt of nodejs niet te installeren is.
# gebruik: tools/test-js.sh   (vanuit de repo-root)
set -eu
command -v podman >/dev/null 2>&1 || { echo "SKIP: podman ontbreekt"; exit 77; }
podman image exists localhost/ravengun-build 2>/dev/null || { echo "SKIP: image localhost/ravengun-build ontbreekt"; exit 77; }
W=src/ravengund/web
T="${TMPDIR:-/tmp}/rg-test-js.$$"
trap 'rm -f "$T"' EXIT
{
  echo "mkdir -p /t"
  echo "cat > /t/core.js <<'RG_EOF_CORE'"; cat $W/calibrate-core.js; echo "RG_EOF_CORE"
  echo "cat > /t/test.js <<'RG_EOF_TEST'"; echo "const RGCal = require('/t/core.js');"; cat tests/js/calibrate_core_test.js; echo "RG_EOF_TEST"
  echo "cat > /t/ui.js <<'RG_EOF_UI'"; cat $W/calibrate.js; echo "RG_EOF_UI"
  echo "cat > /t/app.js <<'RG_EOF_APP'"; cat $W/app.js; echo "RG_EOF_APP"
  echo "cat > /t/appcore.js <<'RG_EOF_ACORE'"; cat $W/app-core.js; echo "RG_EOF_ACORE"
  echo "cat > /t/apptest.js <<'RG_EOF_ATEST'"; echo "const RGApp = require('/t/appcore.js');"; cat tests/js/app_core_test.js; echo "RG_EOF_ATEST"
  echo "node --check /t/core.js && node --check /t/ui.js && node --check /t/app.js && node --check /t/appcore.js && node /t/test.js && node /t/apptest.js"
} > "$T"
podman run --rm -i localhost/ravengun-build sh -c '
  pacman -Sy --noconfirm nodejs >/dev/null 2>&1 || { echo "SKIP: nodejs niet te installeren (geen netwerk?)"; exit 77; }
  sh -s
' < "$T"
