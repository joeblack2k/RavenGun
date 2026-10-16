#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Installeert de PhysX-systeemsoftware van NVIDIA (9.12.0613: de engines 2.3.1 tot en met 2.8.4, 32 en 64 bits) in een Wine-prefix,
# voor games met [Launch] PhysX = true (docs/redist.md, "PhysX"). Een game met PhysX 2.x (PhysXLoader.dll naast de exe) zoekt de
# kern via HKLM\Software\AGEIA Technologies (PhysXCore Path) in Program Files\NVIDIA Corporation\PhysX\Engine\v2.8.x; zonder die
# installatie geeft NxCreatePhysicsSDK NULL en crasht de game (Elevator Action Death Parade, gemeten 2026-10-16).
#
# Het installatiepakket komt van NVIDIA (download, sha256 gecontroleerd) en blijft in ~/.cache/ravengun/downloads/physx; niets
# ervan komt in git. Een prefix waarin PhysX al staat wordt overgeslagen.
#
#   scripts/setup-physx.sh [PREFIX...]     standaard de gedeelde prefix van RavenGun (~/.local/share/ravengun/prefixes/shared/wine)
#
# Omgeving: RG_WINE = de wine van RavenGun (standaard ~/.local/share/ravengun/wine/proton-11.0-2/bin/wine).
set -euo pipefail

URL="https://us.download.nvidia.com/Windows/9.12.0613/PhysX-9.12.0613-SystemSoftware.msi"
SHA256="d363cb55d46d2534ba2ff39fec97cc4e1cd12ad769d96d60c3ee98187c4a77eb"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}/ravengun"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/ravengun/downloads/physx"
WINE="${RG_WINE:-$DATA/wine/proton-11.0-2/bin/wine}"
MSI="$CACHE/$(basename "$URL")"

(( $# )) || set -- "$DATA/prefixes/shared/wine"
[[ -x $WINE ]] || { echo "setup-physx: wine niet gevonden ($WINE); zet RG_WINE" >&2; exit 1; }

mkdir -p "$CACHE"
if [[ ! -s $MSI ]] || ! echo "$SHA256  $MSI" | sha256sum -c --status; then
  echo "setup-physx: download $URL"
  curl -fsSL --retry 3 -o "$MSI.part" "$URL"
  echo "$SHA256  $MSI.part" | sha256sum -c --status || { rm -f "$MSI.part"; echo "setup-physx: sha256 klopt niet" >&2; exit 1; }
  mv "$MSI.part" "$MSI"
fi

for pfx in "$@"; do
  if [[ -f "$pfx/drive_c/Program Files (x86)/NVIDIA Corporation/PhysX/Engine/v2.8.1/PhysXCore.dll" ]]; then
    echo "setup-physx: staat al in $pfx"
    continue
  fi
  [[ -d $pfx/drive_c ]] || { echo "setup-physx: $pfx is geen Wine-prefix" >&2; exit 1; }
  echo "setup-physx: installeren in $pfx"
  WINEPREFIX="$pfx" WINEDEBUG=-all WINESERVER="$(dirname "$WINE")/wineserver" "$WINE" msiexec /i "Z:${MSI//\//\\}" /qn
  WINEPREFIX="$pfx" WINESERVER="$(dirname "$WINE")/wineserver" "$(dirname "$WINE")/wineserver" -w
  [[ -f "$pfx/drive_c/Program Files (x86)/NVIDIA Corporation/PhysX/Engine/v2.8.1/PhysXCore.dll" ]] ||
    { echo "setup-physx: installatie in $pfx mislukt (geen Engine/v2.8.1)" >&2; exit 1; }
done
