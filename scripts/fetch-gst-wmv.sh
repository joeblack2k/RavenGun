#!/usr/bin/env bash
# Haalt de GStreamer-plug-ins asf (gst-plugins-ugly) en libav (gst-libav) uit de SteamOS-pakketrepo en pakt ze lokaal uit
# (geen root, niets systeemwijd). Nodig voor games die .wmv-films afspelen via winegstreamer (Luigi's Mansion Arcade:
# title_movie.wmv e.d.); zonder deze plug-ins sluit de game af of blijft het scherm zwart.
# Resultaat: ~/.local/share/ravengun/tools/gst-extra/plugins ; gebruik: GST_PLUGIN_PATH=<die map> in UserProfiles/<P>.env.
# De versie volgt de lokaal geïnstalleerde gstreamer (pacman -Q), want de plug-ins moeten bij die versie passen.
set -euo pipefail
DEST="${1:-$HOME/.local/share/ravengun/tools/gst-extra}"
VER=$(pacman -Q gstreamer | awk '{print $2}')
REPO=$(pacman -Si gst-libav | sed -n 's/^Repository *: //p')
BASE="https://steamdeck-packages.steamos.cloud/archlinux-mirror/$REPO/os/x86_64"
mkdir -p "$DEST/dl" "$DEST/x" "$DEST/plugins"
for p in gst-libav gst-plugins-ugly; do
  f="$p-$VER-x86_64.pkg.tar.zst"
  [[ -f "$DEST/dl/$f" ]] || curl -fsSL -o "$DEST/dl/$f" "$BASE/$f"
  # Controle tegen de SHA-256 uit de (door pacman geverifieerde) sync-database van het systeem.
  want=$(tar -xOf "$(pacman-conf DBPath)/sync/$REPO.db" --wildcards "$p-$VER/desc" | awk '/^%SHA256SUM%/{getline; print; exit}')
  have=$(sha256sum "$DEST/dl/$f" | awk '{print $1}')
  if [[ -z "$want" || "$want" != "$have" ]]; then
    rm -f "$DEST/dl/$f"; echo "SHA-256 van $f klopt niet met de pacman-database; afgebroken" >&2; exit 1
  fi
  tar --zstd -xf "$DEST/dl/$f" -C "$DEST/x"
done
cp "$DEST"/x/usr/lib/gstreamer-1.0/libgstlibav.so "$DEST"/x/usr/lib/gstreamer-1.0/libgstasf.so "$DEST/plugins/"
GST_PLUGIN_PATH="$DEST/plugins" gst-inspect-1.0 asfdemux >/dev/null && GST_PLUGIN_PATH="$DEST/plugins" gst-inspect-1.0 avdec_wmv3 >/dev/null
echo "klaar: GST_PLUGIN_PATH=$DEST/plugins"
