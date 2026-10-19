#!/usr/bin/env bash
# Zet de archiefboom van een release in elkaar uit een installatieboom (cmake --install), de gebouwde onderdelen en de patches.
# Aangeroepen door tools/dev/make-release.sh; los te draaien (en te testen: tests/release_layout_test.sh).
#
# Gebruik: release-assemble.sh BOOM ONDERDELEN PATCHES [--readme BESTAND]
#   BOOM        de archiefmap (bevat bin/, lib/, share/ravengun/ van cmake --install); wordt aangevuld
#   ONDERDELEN  map met een submap per onderdeel-id (openparrot/, jvsbridge/, ...); komt in share/ravengun/components/
#   PATCHES     de patches-map uit de RavenGun-checkout; alleen de levering gaat mee (tools/dev/ship-patches.py)
#
# Resultaat in BOOM: RavenGun.desktop (dubbelklik in Dolphin start bin/ravengun), README.md, LICENSE, bin/, lib/, share/. Geen
# installatiescript: de app doet de installatie (stap "Install RavenGun" van de wizard).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/../.." && pwd)"
[[ $# -ge 3 ]] || { sed -n '2,13p' "$0"; exit 2; }
S="$1"; COMP="$2"; PATCHES="$3"; shift 3
README="$ROOT/README.md"
while [[ $# -gt 0 ]]; do case "$1" in --readme) README="$2"; shift 2 ;; *) echo "onbekende optie: $1" >&2; exit 2 ;; esac; done
D="$S/share/ravengun"
[[ -x $S/bin/ravengun && -x $S/bin/ravengund && -d $D ]] || { echo "geen volledige installatieboom in $S" >&2; exit 1; }

install -m 755 "$ROOT/packaging/release/RavenGun.desktop" "$S/RavenGun.desktop"
cp "$D/LICENSE" "$S/LICENSE"
cp "$README" "$S/README.md"

# Onderdelen die wij zelf gebouwd hebben: share/ravengun/components/<id>/ (packaging/components.json, kind "bundled").
mkdir -p "$D/components"
for c in "$COMP"/*/; do
  [[ -d $c ]] || continue
  cp -a "${c%/}" "$D/components/"
done

# De patches per game: alleen de levering.
[[ -d $PATCHES ]] || { echo "patches-map ontbreekt: $PATCHES" >&2; exit 1; }
# Geen oude echte datamap of achtergebleven privébestanden naast de nieuwe levering.
[[ "$(realpath -m "$PATCHES")" != "$(realpath -m "$S/patches")" ]] || { echo "patches-bron en release-doel zijn gelijk" >&2; exit 1; }
rm -rf "$D/patches" "$S/patches"
# Het merkbestand van cmake --install (cmake/PatchesLayout.cmake) hoort bij een installatie, niet in een archief.
rm -f "$D/.patches-installatie"
python3 -I "$HERE/ship-patches.py" "$PATCHES" "$S/patches"
ln -s ../../patches "$D/patches"

# Vaste rechten: mappen 755; bestanden 755 als ze uitvoerbaar zijn, anders 644. Het startbestand moet uitvoerbaar zijn.
find "$S" -type d -exec chmod 755 {} +
find "$S" -type f -perm -u+x -exec chmod 755 {} +
find "$S" -type f ! -perm -u+x -exec chmod 644 {} +
