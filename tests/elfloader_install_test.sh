#!/usr/bin/env bash
# Faalt als de geINSTALLEERDE rg-elfloader verschilt van de GEBOUWDE. Precies de valkuil die een keer
# optrad: de gebouwde lader in tools/ had de DT_GNU_HASH-reparatie, maar ~/.local/share/ravengun/bin/
# liep achter, waardoor een benchrun de oude lader mat en ten onrechte "werkt niet" concludeerde.
# scripts/build-elfloader.sh installeert nu en controleert dit zelf; deze test bewaakt het ook in ctest.
#
# Dezelfde valkuil een stap eerder: de BRON is gewijzigd maar er is niet herbouwd. Dan zijn gebouwd en
# geinstalleerd gelijk en allebei oud. Daarom bouwt de test de bron opnieuw (scripts/build-elfloader.sh
# --alleen-bouwen, dezelfde vlaggen, geen installatie) en eist dezelfde sha256; de bouw is gemeten
# reproduceerbaar, ook vanuit een andere map. Zonder gcc -m32 vervalt alleen deze stap.
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

T="$(mktemp -d)" || exit 1
trap 'rm -rf "$T"' EXIT
if ! bash "$REPO/scripts/build-elfloader.sh" --alleen-bouwen "$T/rg-elfloader" >"$T/bouw.log" 2>&1; then
  if command -v "${CC:-gcc}" >/dev/null 2>&1 && printf 'int main(void){return 0;}\n' >"$T/p.c" &&
     "${CC:-gcc}" -m32 -I"$REPO/tools/rg-elfloader/inc" -o "$T/p" "$T/p.c" >/dev/null 2>&1; then
    cat "$T/bouw.log" >&2
    echo "FAIL: de lader-bron bouwt niet meer." >&2
    exit 1
  fi
  echo "bron niet vergeleken: geen 32-bits toolchain (gcc -m32)."
  exit 0
fi
ssha="$(sha256sum "$T/rg-elfloader" | cut -d' ' -f1)"
echo "uit de bron:   $ssha  $REPO/tools/rg-elfloader/rg_elfloader.c"
if [[ "$ssha" != "$bsha" ]]; then
  echo "FAIL: de gebouwde lader komt niet uit de huidige bron. Herbouw met scripts/build-elfloader.sh." >&2
  exit 1
fi
echo "ok: de gebouwde lader komt uit de huidige bron."
