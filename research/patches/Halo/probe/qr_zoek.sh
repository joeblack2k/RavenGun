#!/bin/bash
# WAAR KOMEN DE QR- EN FABRIEKSMELDINGEN VANDAAN? Lezen, geen run.
set -u
H="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo"
UP=~/.local/share/ravengun/native/Halo/0/upper
TMP=$(mktemp -d /tmp/qr-XXXX)

echo "############ 1. de letterlijke teksten in de spelbinary (met de omgeving eromheen)"
for BIN in "$H/game" "$H/game_original"; do
  [ -f "$BIN" ] || continue
  echo "  === $(basename "$BIN") ($(stat -c%s "$BIN") bytes)"
  for T in "QR scanner" "QR Scanner" "Factory Setup" "not detected" "need to be configured"; do
    off=$(grep -abo "$T" "$BIN" 2>/dev/null | head -1 | cut -d: -f1)
    [ -n "$off" ] && printf '    %-24s op offset 0x%x\n' "$T" "$off"
  done
done

echo
echo "############ 2. de omgeving van die teksten (strings in een venster van 512 bytes)"
BIN="$H/game"
for T in "QR Scanner is not detected" "Factory Setup Has Not Been Completed" "QR scanners need to be configured"; do
  off=$(grep -abo "$T" "$BIN" 2>/dev/null | head -1 | cut -d: -f1)
  [ -z "${off:-}" ] && { echo "  '$T' niet gevonden"; continue; }
  s=$(( off > 256 ? off - 256 : 0 ))
  echo "  === '$T' op offset 0x$off; de strings in het venster eromheen:"
  dd if="$BIN" bs=1 skip=$s count=640 2>/dev/null | strings -n 5 | head -25 | sed 's/^/    /'
done

echo
echo "############ 3. waar staat die tekst in het ADRES van het beeld, en wie verwijst ernaar?"
readelf -lW "$BIN" 2>/dev/null | awk '/LOAD/{print "    LOAD off 0x"$1" vaddr "$3" filesz "$5}' | head -4
python3 - "$BIN" <<'PY'
import struct, subprocess, sys
bin = sys.argv[1]
d = open(bin, "rb").read()
# PT_LOAD-tabel uit de program headers
lf = 0x40 if d[:4] == b"\x7fELF" else 0
phoff = struct.unpack_from("<Q", d, 0x20)[0]
phentsize, phnum = struct.unpack_from("<HH", d, 0x36)
segs = []
for i in range(phnum):
    o = phoff + i * phentsize
    if struct.unpack_from("<I", d, o)[0] == 1:  # PT_LOAD
        off, va, pa, fsz = struct.unpack_from("<QQQQ", d, o + 8)
        segs.append((off, va, fsz))
def off2va(off):
    for o, va, fsz in segs:
        if o <= off < o + fsz:
            return va + (off - o)
    return None
for t in (b"QR scanners need to be configured", b"Factory Setup Has Not Been Completed",
          b"QR Scanner is not detected", b"is not detected"):
    i = d.find(t)
    if i < 0:
        continue
    va = off2va(i)
    print("  %-40s offset 0x%x -> vaddr %s" % (t.decode(), i, hex(va) if va else "?"))
PY

echo
echo "############ 4. de eigen bestanden van de game: staat daar iets over QR/fabriek in?"
find "$UP" -maxdepth 3 2>/dev/null | head -20 | sed 's/^/  /'
echo "  --- en de inhoud van de mappen die de game zelf aanmaakt:"
for m in adj aud messages querycache; do
  [ -d "$UP/pmuser/$m" ] && { echo "    $m:"; ls -l "$UP/pmuser/$m" 2>/dev/null | head -6 | sed 's/^/      /'; }
done
echo "  --- zoeken naar qr/scanner/factory/setup in alles wat de game schrijft:"
grep -rliE 'qr|scanner|factory|setup' "$UP" 2>/dev/null | head -10 | sed 's/^/    /'

echo
echo "############ 5. configuratie- en toestandsbestanden in de spelmap"
find "$H" -maxdepth 2 \( -iname '*.ini' -o -iname '*.cfg' -o -iname '*.conf' -o -iname '*.json' -o -iname '*.dat' -o -iname '*.xml' -o -iname '*.txt' \) 2>/dev/null | head -15 | sed 's/^/  /'
echo "  --- en de map 'data' (waar de .g7-bestanden staan):"
ls -1 "$H/data" 2>/dev/null | head -10 | sed 's/^/    /'
echo "  --- eFileMaps (het manifest): $(ls -1 "$H/eFileMaps" 2>/dev/null | wc -l) kaarten"

echo
echo "############ 6. zoekt de game naar een SERVICE-menu of installatiestap?"
strings -n 6 "$BIN" 2>/dev/null | grep -iE 'factory|setup|install|service mode|operator|scanner|qr' | sort -u | head -25 | sed 's/^/  /'
echo "== done ($TMP)"
