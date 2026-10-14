#!/usr/bin/env bash
# Haalt DXVK (Direct3D 8/9/10/11 via Vulkan, zlib-licentie) op voor games met [Launch] Dxvk = true en pakt het lokaal uit
# (geen root, niets in een prefix). rg-direct zet de DLL's van de juiste bitbreedte als drop-in in de view van de game
# (d3d8, d3d9, d3d10core, d3d11, dxgi met WINEDLLOVERRIDES <naam>=n); de gedeelde prefix blijft ongemoeid.
# Resultaat: ~/.local/share/ravengun/dxvk/dxvk-<versie>/{x32,x64} en de link ~/.local/share/ravengun/dxvk/current.
# Zonder deze map zoekt rg-direct DXVK in Proton/GE-Proton van Steam (files/lib/wine/dxvk); zie docs/dxvk.md.
#
#   scripts/fetch-dxvk.sh [DOEL]
set -euo pipefail
VER=2.7.1
# SHA-256 van de officiele release (github.com/doitsujin/dxvk/releases/tag/v2.7.1), vastgelegd bij de eerste download op 2026-10-15.
SHA=d85ce7c79f57ecd765aaa1b9e7007cb875e6fde9f6d331df799bce73d513ce87
DEST="${1:-$HOME/.local/share/ravengun/dxvk}"
URL="https://github.com/doitsujin/dxvk/releases/download/v$VER/dxvk-$VER.tar.gz"
mkdir -p "$DEST/dl"
f="$DEST/dl/dxvk-$VER.tar.gz"
if [[ ! -f "$f" ]]; then
  curl -fsSL -o "$f.part" "$URL"
  mv "$f.part" "$f"
fi
have=$(sha256sum "$f" | awk '{print $1}')
if [[ "$have" != "$SHA" ]]; then
  mv "$f" "$f.fout"; echo "SHA-256 van dxvk-$VER.tar.gz klopt niet ($have, verwacht $SHA); afgebroken (bestand: $f.fout)" >&2; exit 1
fi
tar -xzf "$f" -C "$DEST"
for a in x32 x64; do
  for d in d3d8 d3d9 d3d10core d3d11 dxgi; do
    [[ -f "$DEST/dxvk-$VER/$a/$d.dll" ]] || { echo "dxvk-$VER/$a/$d.dll ontbreekt na het uitpakken" >&2; exit 1; }
  done
done
echo "dxvk $VER (release, sha256 $SHA)" > "$DEST/dxvk-$VER/version"
ln -sfn "dxvk-$VER" "$DEST/current"
echo "klaar: $DEST/current -> dxvk-$VER"
