#!/bin/bash
# WELK KANAAL GEBRUIKT TEKNOPARROT VOOR DE AIM VAN HALO? Lezen, niets starten.
set -u
TP=/home/deck/mnt/spidernas_emulation/Teknoparrot
R=~/projects/ravengun

echo "############ 1. de IO/JVS-sectie van het Halo-document"
sed -n '150,203p' "$TP/findings/16_halo_fireteam.md" 2>/dev/null | sed 's/^/  /'

echo
echo "############ 2. alles in dat document over aim, lichtgeweer, positie, JVS en het bord"
grep -niE 'aim|light ?gun|lichtgeweer|positie|position|JVS|board|bord|gun|trigger|trekker|0x180a|0x61e|offset' \
  "$TP/findings/16_halo_fireteam.md" 2>/dev/null | head -40 | sed 's/^/  /'

echo
echo "############ 3. de JVS-specificatie zelf"
find "$TP" "$R/docs" -iname '*jvs*' 2>/dev/null | head -8 | sed 's/^/  /'
JVS=$(find "$TP" -iname '*jvs*spec*' 2>/dev/null | head -1)
if [ -n "${JVS:-}" ]; then
  echo "  --- $JVS ($(wc -l < "$JVS") regels):"
  grep -niE 'aim|position|lichtgeweer|light ?gun|absolute|analoog|analog|X|Y' "$JVS" 2>/dev/null | head -25 | sed 's/^/    /'
fi

echo
echo "############ 4. wat er in het project al over de AIM van Halo bekend is"
grep -rniE 'aim|trekker|trigger|lichtgeweer|lightgun' "$R/packaging/games-status.md" "$R/docs/decisions.md" 2>/dev/null | grep -i halo | head -12 | sed 's/^/  /'
echo "  --- en de Halo-dossiers in de patches-repo:"
grep -rniE 'aim|trekker|trigger|JVS|bord|board' ~/projects/ravengun-patches/patches/Halo/*.md 2>/dev/null | grep -viE 'geen invoer|niet getest' | head -12 | sed 's/^/  /'

echo
echo "############ 5. de WartranTroopers-indeling waar de IO-brug mee werkt"
grep -rniE 'WartranTroopers|control 32 bits|offset 8|aim 1 byte|offset 12' "$R/docs"/*.md "$R/src" -r 2>/dev/null | head -12 | sed 's/^/  /'
echo "== done"
