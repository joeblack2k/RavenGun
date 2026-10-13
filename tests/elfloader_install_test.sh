#!/usr/bin/env bash
# Faalt als de geINSTALLEERDE rg-elfloader verschilt van de GEBOUWDE. Precies de valkuil die een keer
# optrad: de gebouwde lader in tools/ had de DT_GNU_HASH-reparatie, maar ~/.local/share/ravengun/bin/
# liep achter, waardoor een benchrun de oude lader mat en ten onrechte "werkt niet" concludeerde.
# scripts/build-elfloader.sh installeert nu en controleert dit zelf; deze test bewaakt het ook in ctest.
# Overgeslagen (77) als er geen gebouwde lader is (een verse checkout zonder scripts/build-elfloader.sh).
# Gebruik: elfloader_install_test.sh <repo>
set -u
REPO="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
BUILT="$REPO/tools/rg-elfloader/rg-elfloader"
INST="${RAVENGUN_ELFLOADER_INSTALLED:-$HOME/.local/share/ravengun/bin/rg-elfloader}"

[[ -x "$BUILT" ]] || { echo "overgeslagen: geen gebouwde lader ($BUILT; draai scripts/build-elfloader.sh)"; exit 77; }
if [[ ! -e "$INST" ]]; then
  echo "FAIL: er is een gebouwde lader maar geen geinstalleerde ($INST). Draai scripts/build-elfloader.sh." >&2
  exit 1
fi
bsha="$(sha256sum "$BUILT" | cut -d' ' -f1)"
isha="$(sha256sum "$INST" | cut -d' ' -f1)"
echo "gebouwd:     $bsha  $BUILT"
echo "geinstalleerd: $isha  $INST"
if [[ "$bsha" != "$isha" ]]; then
  echo "FAIL: de geinstalleerde lader verschilt van de gebouwde. Herinstalleer met scripts/build-elfloader.sh." >&2
  exit 1
fi
echo "ok: de geinstalleerde lader is de gebouwde."
