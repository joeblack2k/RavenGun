#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Bouwt de diagnostische ExitProcess-proef (rg_exit_proef.c) met mingw in de podman-image van het
# project. De uitvoer staat BUITEN de repo.
#
#   bash patches/LuigisMansion/probe/bouw-exit-proef.sh
#
#   RG_EXITPROEF_OUT     uitvoermap (standaard ~/.local/share/ravengun/b0/exit-proef)
#   RG_MINGW_IMAGE       image (standaard localhost/ravengun-mingw)
#
# Een bouw hier kan STIL mislukken (een oude DLL blijft dan staan en lijkt nieuw). Daarom:
#   - de oude DLL wordt VOOR de bouw weggehaald;
#   - de afsluitstatus van de compiler wordt apart gemeld, niet via een pijp;
#   - de DLL moet daarna bestaan, groter dan nul zijn, een PE zijn en een mtime >= de starttijd hebben;
#   - er wordt een tweede keer gebouwd en de sha256 moet gelijk zijn (reproduceerbaar);
#   - de imports worden getoond; alles buiten kernel32 is een FOUT (geen CRT: -nostdlib).
set -uo pipefail

HIER="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="${RG_MINGW_IMAGE:-localhost/ravengun-mingw}"
OUT="${RG_EXITPROEF_OUT:-$HOME/.local/share/ravengun/b0/exit-proef}"
DLL="$OUT/rg-exit-proef.dll"
TWEEDE="$OUT/.tweede"

fout() { printf 'FOUT: %s\n' "$*" >&2; exit 1; }

command -v podman >/dev/null 2>&1 || fout "podman ontbreekt"
podman image exists "$IMAGE" || fout "de image $IMAGE bestaat niet (bouw hem eerst; zie scripts/build-hasp.sh)"
[[ -f "$HIER/rg_exit_proef.c" ]] || fout "$HIER/rg_exit_proef.c ontbreekt"
case "$OUT" in "$HOME/projects/ravengun"*) fout "de uitvoermap $OUT ligt in de repo; kies een map erbuiten" ;; esac

mkdir -p "$OUT" "$TWEEDE" || fout "kan $OUT niet maken"
rm -f "$DLL" "$TWEEDE/rg-exit-proef.dll"
START="$(date +%s)"
sleep 1                      # mtime heeft seconde-resolutie; een bestand van deze run is strikt jonger

bouw() {                     # $1 = uitvoermap
  podman run --rm --network=none -v "$HIER":/src:ro,Z -v "$1":/out:Z -w /out "$IMAGE" \
    x86_64-w64-mingw32-gcc -shared -O2 -Wall -Wextra -o rg-exit-proef.dll /src/rg_exit_proef.c \
    -nostdlib -Wl,-e,DllMain -lkernel32 -Wl,--no-insert-timestamp
}

bouw "$OUT"
rc=$?
echo "compiler-afsluitstatus (bouw 1): $rc"
[[ $rc -eq 0 ]] || fout "de compiler gaf status $rc"
[[ -s "$DLL" ]] || fout "$DLL bestaat niet of is leeg na een geslaagde bouw"
[[ "$(stat -c %Y "$DLL")" -gt "$START" ]] || fout "$DLL is niet vers geschreven (mtime <= start van deze run)"

bouw "$TWEEDE"
rc=$?
echo "compiler-afsluitstatus (bouw 2): $rc"
[[ $rc -eq 0 ]] || fout "de tweede bouw gaf status $rc"
[[ -s "$TWEEDE/rg-exit-proef.dll" ]] || fout "tweede bouw leverde geen DLL"
s1="$(sha256sum "$DLL" | cut -d' ' -f1)"
s2="$(sha256sum "$TWEEDE/rg-exit-proef.dll" | cut -d' ' -f1)"
rm -rf "$TWEEDE"
[[ "$s1" == "$s2" ]] || fout "NIET reproduceerbaar: $s1 != $s2"

ls -la --time-style=full-iso "$DLL"
sha256sum "$DLL"
file "$DLL"
echo "-- imports (objdump -p in de container):"
imports="$(podman run --rm --network=none -v "$OUT":/out:ro,Z "$IMAGE" \
  x86_64-w64-mingw32-objdump -p /out/rg-exit-proef.dll | grep 'DLL Name')" || fout "objdump mislukt"
echo "$imports"
if echo "$imports" | grep -viE 'DLL Name: (kernel32)\.dll' | grep -q .; then
  fout "onverwachte imports (alleen kernel32 toegestaan)"
fi
echo "OK: $DLL  sha256=$s1  (twee bouwen identiek)"
