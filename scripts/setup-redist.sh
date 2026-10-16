#!/usr/bin/env bash
# Zet de DLL's uit de DirectX-herdistribueerbare die Steam al meebrengt klaar voor games met [Launch] NativeDlls (docs/redist.md):
# XACT (xactengine3_*), XAudio2, X3DAudio, D3DX9/10/11, d3dcompiler, XInput. Bron: Steamworks Shared/_CommonRedist/DirectX/Jun2010
# van de eigen Steam-installatie; er wordt niets gedownload en niets komt in git (Microsoft-bestanden blijven in de gebruikersmap).
#   ~/.local/share/ravengun/redist/x86/<naam>.dll   uit *_x86.cab
#   ~/.local/share/ravengun/redist/x64/<naam>.dll   uit *_x64.cab
#
#   scripts/setup-redist.sh [DOEL]
set -euo pipefail
DEST="${1:-${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/redist}"
SRC=""
for d in "$HOME/.local/share/Steam/steamapps/common/Steamworks Shared/_CommonRedist/DirectX/Jun2010" \
         /run/media/*/*/SteamLibrary/steamapps/common/"Steamworks Shared"/_CommonRedist/DirectX/Jun2010; do
  [[ -d "$d" ]] && ls "$d"/*_x86.cab >/dev/null 2>&1 && { SRC="$d"; break; }
done
[[ -n "$SRC" ]] || { echo "geen DirectX-herdistribueerbare van Steam gevonden (Steamworks Shared/_CommonRedist/DirectX/Jun2010)" >&2; exit 1; }
extract() {   # extract CAB DOELMAP: alle DLL's uit de cab
  if command -v 7z >/dev/null; then 7z e -y -o"$2" "$1" '*.dll' >/dev/null
  else bsdtar -xf "$1" -C "$2" '*.dll' 2>/dev/null || bsdtar -xf "$1" -C "$2"; fi
}
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
for arch in x86 x64; do
  mkdir -p "$T/$arch" "$DEST/$arch"
  for c in "$SRC"/*_"$arch".cab; do extract "$c" "$T/$arch"; done
  n=0
  for f in "$T/$arch"/*.dll; do
    [[ -f "$f" ]] || continue
    b=$(basename "$f" | tr 'A-Z' 'a-z')
    install -m 0644 "$f" "$DEST/$arch/$b"; n=$((n + 1))
  done
  echo "$arch: $n DLL's"
done
# Visual C++ 2010 (MFC en CRT: mfc100u, mfcm100u, msvcr100, msvcp100, atl100) uit _CommonRedist/vcredist/2010: het installatiepakket
# zet onder Wine niets neer, dus de DLL's komen er net als DirectX via [Launch] NativeDlls in (Lost Land Adventure: mfc100u, 2026-10-16).
VC="$(dirname "$(dirname "$SRC")")/vcredist/2010"
if [[ -f "$VC/vcredist_x64.exe" ]] && command -v 7z >/dev/null; then
  for arch in x86 x64; do
    [[ -f "$VC/vcredist_$arch.exe" ]] || continue
    mkdir -p "$T/vc$arch/cab"
    7z x -y -o"$T/vc$arch" "$VC/vcredist_$arch.exe" >/dev/null
    for c in "$T/vc$arch"/*.cab; do [[ -f $c ]] && 7z x -y -o"$T/vc$arch/cab" "$c" >/dev/null; done
    n=0
    for naam in mfc100u mfcm100u mfc100 msvcr100 msvcp100 atl100; do
      f="$T/vc$arch/cab/F_CENTRAL_${naam}_$arch"
      [[ -f $f ]] && { install -m 0644 "$f" "$DEST/$arch/$naam.dll"; n=$((n + 1)); }
    done
    echo "$arch: $n DLL's uit Visual C++ 2010"
  done
fi
echo "bron: $SRC" > "$DEST/BRON.txt"
echo "klaar: $DEST"
