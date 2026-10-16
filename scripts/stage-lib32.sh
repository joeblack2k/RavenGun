#!/usr/bin/env bash
# RavenGun: zet de i386-bibliotheken klaar die de native profielen (patches/<Game>/native.json; vroeger packaging/native-games.json) met @lib32 noemen (~/.local/share/ravengun/runtime/lib32),
# voor de native route van de daemon (src/ravengun/launch/native.*: "bibliotheken ontbreken" zonder deze map). Alleen
# bibliotheken, geen game: uit de Steam Linux Runtime (sniper) en sdl12-compat uit de multilib-opslagplaats van SteamOS.
# Eenmalig; opnieuw draaien kan altijd. Was `scripts/play-native.sh --stage-libs` (dat script is weg: de daemon start de game zelf).
# Gebruik: scripts/stage-lib32.sh
set -euo pipefail

DATA="${XDG_DATA_HOME:-$HOME/.local/share}/ravengun"
LIB32="$DATA/runtime/lib32"
die() { echo "stage-lib32: $*" >&2; exit 1; }
[[ $# -eq 0 ]] || { sed -n '2,6p' "$0"; exit 2; }

# i386-bibliotheken die niet in /usr/lib32 of in de ElfLdr2-map staan, uit de Steam Linux Runtime (alleen bibliotheken, geen game).
mkdir -p "$LIB32"
SN=""
for c in /run/media/deck/steam/SteamLibrary/steamapps/common/SteamLinuxRuntime_sniper/var/tmp-*/usr/lib/i386-linux-gnu \
         "$HOME"/.local/share/Steam/steamapps/common/SteamLinuxRuntime_sniper/var/tmp-*/usr/lib/i386-linux-gnu; do
  [[ -e "$c/libglut.so.3" ]] && SN="$c" && break
done
[[ -n "$SN" ]] || die "geen i386-runtime met libglut gevonden (Steam Linux Runtime sniper)"
# libsamplerate/libXcursor/libxkbcommon/libdecor: wat de i386-libSDL2 hieronder zelf vraagt en SteamOS niet voor i386 heeft
# (zonder: "Failed loading SDL2 library", gemeten 2026-10-13 op Jurassic Park).
for l in libglut.so.3 libXi.so.6 libGLU.so.1 libtheora.so.0 libXrandr.so.2 libXrender.so.1 \
         libsamplerate.so.0 libXcursor.so.1 libxkbcommon.so.0 libdecor-0.so.0; do
  [[ -e "$SN/$l" ]] || { echo "ontbreekt in de runtime: $l" >&2; continue; }
  cp -L --no-preserve=ownership "$SN/$l" "$LIB32/$l"; echo "gezet: $LIB32/$l"
done
# libSDL-1.2 (voor Jurassic Park) is er niet voor i386: sdl12-compat uit de multilib-opslagplaats van SteamOS, uitgepakt in een eigen tijdelijke map.
if [[ ! -e "$LIB32/libSDL-1.2.so.0" ]]; then
  for l in libSDL2-2.0.so.0; do [[ -e "$SN/$l" ]] && cp -L "$SN/$l" "$LIB32/$l" && echo "gezet: $LIB32/$l"; done
  T=$(mktemp -d); curl -fsS --max-time 60 -o "$T/p.tar.zst" "https://steamdeck-packages.steamos.cloud/archlinux-mirror/multilib-3.9/os/x86_64/lib32-sdl12-compat-1.2.68-2-x86_64.pkg.tar.zst" \
    && mkdir "$T/x" && tar -C "$T/x" -xf "$T/p.tar.zst" && cp -L "$T/x/usr/lib32/libSDL-1.2.so.1.2.68" "$LIB32/libSDL-1.2.so.0" && echo "gezet: $LIB32/libSDL-1.2.so.0" \
    || echo "libSDL-1.2 ophalen mislukt" >&2
  rm -rf "$T"
fi
