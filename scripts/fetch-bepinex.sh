#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Haalt de officiële release van BepInEx (Mono, Windows) op voor games met [Launch] BepInEx = <versie> (docs/per-game-patches.md,
# "BepInEx op de directe route") en voor de Unity-route (plugins/unity/README.md). BepInEx (LGPL-2.1) en HarmonyX (MIT) komen van
# github.com/BepInEx; de zip wordt met sha256 gecontroleerd en uitgepakt naar <data>/bepinex/<versie>/<x64|x86>. Niets ervan komt in git.
# Een versie die al uitgepakt staat wordt overgeslagen.
#
#   scripts/fetch-bepinex.sh [VERSIE]     standaard 5.4.21.0 (beide bitbreedtes)
set -euo pipefail

VERSIE="${1:-5.4.21.0}"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}/ravengun"
DEST="$DATA/bepinex/$VERSIE"

declare -A SHA
case "$VERSIE" in
  5.4.21.0)
    SHA[x64]=2af69fe0aaf821e95c4cd3e4d53860e667c54648f97dca6f971a5bcd3c22aa34
    SHA[x86]=f89f3f6b97e8f486ec8eeea489415d058233240fd6de79b3cbdd90e601fa6bbf
    TAG=v5.4.21 ;;
  *) echo "fetch-bepinex: versie $VERSIE is niet vastgelegd (alleen 5.4.21.0); voeg de sha256 van de release toe" >&2; exit 1 ;;
esac

mkdir -p "$DEST/zip"
for arch in x64 x86; do
  if [[ -f $DEST/$arch/BepInEx/core/BepInEx.Preloader.dll && -f $DEST/$arch/winhttp.dll ]]; then
    echo "fetch-bepinex: $VERSIE $arch staat al in $DEST/$arch"
    continue
  fi
  zip="$DEST/zip/BepInEx_${arch}_$VERSIE.zip"
  if [[ ! -s $zip ]] || ! echo "${SHA[$arch]}  $zip" | sha256sum -c --status; then
    url="https://github.com/BepInEx/BepInEx/releases/download/$TAG/BepInEx_${arch}_$VERSIE.zip"
    echo "fetch-bepinex: download $url"
    curl -fsSL --retry 3 -o "$zip.part" "$url"
    echo "${SHA[$arch]}  $zip.part" | sha256sum -c --status || { rm -f "$zip.part"; echo "fetch-bepinex: sha256 van $arch klopt niet" >&2; exit 1; }
    mv "$zip.part" "$zip"
  fi
  rm -rf "$DEST/$arch.part"
  mkdir -p "$DEST/$arch.part"
  (cd "$DEST/$arch.part" && unzip -q "$zip")
  [[ -f $DEST/$arch.part/BepInEx/core/BepInEx.Preloader.dll ]] || { echo "fetch-bepinex: $zip bevat geen BepInEx/core" >&2; exit 1; }
  rm -rf "$DEST/$arch"
  mv "$DEST/$arch.part" "$DEST/$arch"
  echo "fetch-bepinex: $VERSIE $arch uitgepakt in $DEST/$arch"
done
