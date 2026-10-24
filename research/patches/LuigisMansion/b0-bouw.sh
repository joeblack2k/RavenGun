#!/usr/bin/env bash
# Bouwt de B0-DLL's in de MinGW-container. Geen netwerk, geen prefix, geen game.
#
# Twee DLL's, zodat de bisectie scherp is:
#   leeg.dll   -- entry point IS DllMain (die TRUE teruggeeft), geen CRT, geen UCRT-imports
#   leeg0.dll  -- geen entry point, geen imports: alleen de module wordt gemapt
# Beide met een geforceerde .reloc, zodat ze verplaatsbaar zijn en LoadLibraryW niet op een
# bezet ImageBase stukloopt.
set -eu
HIER="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
B0="${B0_DIR:-$HOME/.local/share/ravengun/b0}"
IMG="${B0_IMAGE:-localhost/ravengun-mingw}"
BASE="${B0_IMAGE_BASE:-0x180000000}"
mkdir -p "$B0/src"

cp -f "$HIER/b0-leeg.c"  "$B0/src/leeg.c"
cp -f "$HIER/b0-leeg0.c" "$B0/src/leeg0.c"

echo "== bouwen in $IMG (uit: $B0)"
podman run --rm -v "$B0:/out:Z" -w /out "$IMG" sh -c "
set -e
# B0: DllMain is de entry point. -nostdlib houdt de CRT en de api-ms-win-crt-imports weg.
x86_64-w64-mingw32-gcc -shared -O2 -s -nostdlib -Wl,--entry=DllMain \
    -Wl,--image-base,$BASE -Wl,--no-insert-timestamp -o leeg.dll src/leeg.c
# B0a: helemaal geen entry point en geen imports.
x86_64-w64-mingw32-gcc -shared -O2 -s -nostdlib -Wl,--entry=0 \
    -Wl,--image-base,$BASE -Wl,--no-insert-timestamp -o leeg0.dll src/leeg0.c
"

echo
echo "== controle =="
podman run --rm -v "$B0:/out:Z" -w /out "$IMG" sh -c '
for f in leeg.dll leeg0.dll; do
  echo "--- $f"
  objdump -f "$f" | grep -E "file format|architecture|start address" | sed "s/^/    /"
  printf "    Subsystem/DllChar/Entry: "
  objdump -p "$f" | awk "/AddressOfEntryPoint/{e=\$2} /Subsystem /{s=\$2} /DllCharacteristics/{d=\$2} END{printf \"entry=0x%s subsystem=%s dllchar=%s\n\", e, s, d}"
  printf "    imports: %s   .reloc: %s\n" \
    "$(objdump -p "$f" | grep -c "DLL Name")" \
    "$(objdump -h "$f" | grep -c "\.reloc")"
  objdump -p "$f" | grep "DLL Name" | sed "s/^/      /"
done
'
echo
for f in leeg.dll leeg0.dll; do sha256sum "$B0/$f" | sed 's/^/  /'; done
echo
echo "klaar. De lader krijgt het pad ZONDER .dll als eerste argument, dus:"
echo "  leeg  -> $B0/leeg.dll"
echo "  leeg0 -> $B0/leeg0.dll"

