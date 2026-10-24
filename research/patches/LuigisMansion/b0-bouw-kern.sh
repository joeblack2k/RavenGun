#!/usr/bin/env bash
# b0-bouw-kern.sh -- bouwt ONZE kern ZONDER patch 0005, in een EIGEN map, zonder de actieve
# build aan te raken.
#
# Waarom: patch 0005 blokkeert elke COM-naam met FILE_NOT_FOUND, nagebouwd van een bewering over
# de gesloten kern die in de binary van 1.0.0.3761 niet terug te vinden is. Om te kunnen meten of
# die blokkade helpt of juist schaadt, is een kern zonder 0005 nodig -- naast de bestaande.
#
# Veiligheid: build-openparrot.sh kent --out, dus de actieve map
# ~/.local/share/ravengun/openparrot/current blijft ONGEMOEID. Het patchbestand wordt tijdens de
# build even weggelegd en daarna gegarandeerd teruggezet (trap). Na afloop wordt de sha256 van
# zowel de actieve kern als de nieuwe kern geprint.
set -eu
R="${RG_REPO:-$HOME/projects/ravengun}"
P="$R/third_party/openparrot/patches/0005-luigismansion-com-block.patch"
B0="${B0_DIR:-$HOME/.local/share/ravengun/b0}"
UIT="$B0/core-zonder-0005"
ACTIEF="$HOME/.local/share/ravengun/openparrot/current/OpenParrotx64/OpenParrot64.dll"

[[ -f "$P" ]] || { echo "patch 0005 niet gevonden: $P" >&2; exit 1; }
ACTIEF_SHA_VOOR=$(sha256sum "$ACTIEF" | awk '{print $1}')
echo "actieve kern VOOR: $ACTIEF_SHA_VOOR"

TERUG=0
terugzetten() {
  [[ $TERUG -eq 1 ]] && return 0
  TERUG=1
  if [[ -f /tmp/0005.bewaard ]]; then mv -f /tmp/0005.bewaard "$P"; echo "patch 0005 teruggezet"; fi
}
trap terugzetten EXIT

mv -f "$P" /tmp/0005.bewaard
echo "patch 0005 even weggelegd; aanwezige patches nu:"; ls "$R/third_party/openparrot/patches/"

rm -rf "$UIT"
mkdir -p "$UIT"
echo "bouwen zonder 0005 -> $UIT"
bash "$R/scripts/build-openparrot.sh" --targets "loader64 core64" --out "$UIT"

terugzetten
echo
echo "=== controle ==="
echo -n "actieve kern NA:  "; sha256sum "$ACTIEF" | awk '{print $1}'
[[ "$(sha256sum "$ACTIEF" | awk '{print $1}')" == "$ACTIEF_SHA_VOOR" ]] && echo "  -> ONGEMOEID" || echo "  -> WIJKT AF"
echo -n "kern zonder 0005: "; sha256sum "$UIT/OpenParrotx64/OpenParrot64.dll" | awk '{print $1}'
echo -n "  wijkt af van de actieve? "
[[ "$(sha256sum "$UIT/OpenParrotx64/OpenParrot64.dll" | awk '{print $1}')" != "$ACTIEF_SHA_VOOR" ]] && echo "ja (goed: het is een andere build)" || echo "NEE -- dan is 0005 niet toegepast of de build is niet reproduceerbaar"
echo -n "  zit de Luigi-CRC erin? "
grep -a -c -P '\x03\x96\xdc\xd3' "$UIT/OpenParrotx64/OpenParrot64.dll" 2>/dev/null || true
echo "  (0 = geen Luigi-case, dus 0005 zit er inderdaad niet in; 1 = wel)"

