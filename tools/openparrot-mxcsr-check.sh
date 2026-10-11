#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Meet of de Wine-fix in de eigen OpenParrot-lader werkt: een klein programma leest zijn MXCSR nadat de lader de context van
# zijn hoofdthread heeft gezet. De lader van TeknoParrot (ContextFlags 0x1001F) laat in een 32-bit proces MXCSR op 0 achter
# (alle SSE-uitzonderingen ongemaskeerd, c00002b5 in Mesa); de eigen lader (0x10017) geeft de standaardwaarde 0x1F80.
# Geen game: de "kern" is een lege DLL met alleen InitializeASI. Draait onder Wine in een wegwerp-prefix in een tijdelijke map.
#
# Gebruik: tools/openparrot-mxcsr-check.sh [WINE]      (standaard ~/.local/share/ravengun/wine/proton-11.0-2/bin/wine)
# Vereist: scripts/build-openparrot.sh is gedraaid (build-image en ~/.local/share/ravengun/openparrot/current), de TeknoParrot-runtime.
# Uitvoer: vier regels "MXCSR=0x...". Exit 0 als eigen 32-bit lader 0x1f80 geeft, anders 1.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WINE="${1:-$HOME/.local/share/ravengun/wine/proton-11.0-2/bin/wine}"
TP="${RAVENGUN_TP_RUNTIME:-$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64}"
OWN="${RAVENGUN_OPENPARROT_DIR:-$HOME/.local/share/ravengun/openparrot/current}"
IMAGE="localhost/ravengun-openparrot-build:$(sha256sum "$ROOT/packaging/containers/openparrot/Containerfile" | cut -c1-12)"
T="$(mktemp -d)"; trap 'WINEPREFIX="$T/prefix" "$(dirname "$WINE")/wineserver" -k 2>/dev/null || true; rm -rf "$T"' EXIT
mkdir -p "$T/src" "$T/w"
cat > "$T/src/mxcsr.c" <<'EOF'
#include <stdio.h>
#include <xmmintrin.h>
int main(void)
{
    unsigned int csr = _mm_getcsr();
#ifdef _WIN64
    FILE *f = fopen("mxcsr-64.txt", "w");
#else
    FILE *f = fopen("mxcsr-32.txt", "w");
#endif
    if (f) { fprintf(f, "MXCSR=0x%08x\n", csr); fclose(f); }
    return 0;
}
EOF
cat > "$T/src/dummy.c" <<'EOF'
#include <windows.h>
__declspec(dllexport) void InitializeASI(void) {}
BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID x) { return TRUE; }
EOF
podman run --rm --network=none -v "$T/src:/src:ro" -v "$T/w:/w" "$IMAGE" sh -c '
  i686-w64-mingw32-gcc -O1 -msse -o /w/mxcsr32.exe /src/mxcsr.c && x86_64-w64-mingw32-gcc -O1 -o /w/mxcsr64.exe /src/mxcsr.c &&
  i686-w64-mingw32-gcc -shared -o /w/dummy32.dll /src/dummy.c && x86_64-w64-mingw32-gcc -shared -o /w/dummy64.dll /src/dummy.c'
export WINEPREFIX="$T/prefix" WINEDEBUG=-all WINEDLLOVERRIDES="winemenubuilder.exe=d;mscoree=d;mshtml=d"
wp() { echo "Z:${1//\//\\}"; }   # Unix-pad naar Windows-pad
run() {  # label lader kern exe uitvoerbestand
  rm -f "$T/w/$5"
  (cd "$T/w" && timeout 90 "$WINE" "$2" "$(wp "$3")" "$(wp "$4")" >/dev/null 2>&1) || true
  echo "$1: $(cat "$T/w/$5" 2>/dev/null || echo 'geen uitvoer')"
}
r32="$(run "32-bit, lader van TeknoParrot (0x1001F)" "$TP/OpenParrotWin32/OpenParrotLoader.exe" "$T/w/dummy32" "$T/w/mxcsr32.exe" mxcsr-32.txt | tee /dev/stderr)"
r32o="$(run "32-bit, eigen lader (0x10017)        " "$OWN/OpenParrotWin32/OpenParrotLoader.exe" "$T/w/dummy32" "$T/w/mxcsr32.exe" mxcsr-32.txt | tee /dev/stderr)"
run "64-bit, lader van TeknoParrot (0x1001F)" "$TP/OpenParrotx64/OpenParrotLoader64.exe" "$T/w/dummy64" "$T/w/mxcsr64.exe" mxcsr-64.txt
run "64-bit, eigen lader (0x10017)        " "$OWN/OpenParrotx64/OpenParrotLoader64.exe" "$T/w/dummy64" "$T/w/mxcsr64.exe" mxcsr-64.txt
[[ "$r32o" == *"0x00001f80"* ]]
