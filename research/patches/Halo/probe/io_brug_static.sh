#!/bin/bash
# IO-BRUG-ROUTE, stap 1: BENCH-VRIJ statisch werk. Leest ALLEEN (NAS blijft read-only, geen run).
# Doel: (a) het adresmodel van de schakelaartafel ontwarren (waarom schrijft de hulp op 0x180ae80+0xaea0
#       terwijl de getter vanaf 0x180ae80 rekent?), (b) de twee maskers 0x1766f40 en 0x181d8a0 statisch
#       bekijken en tellen wie ze aanraakt, (c) de zustergetter 0x61ee70 lezen (soort==0 = analoog kanaal).
# Instrument: RIP-scanner met de HERSTELDE formule. Kalibratie vooraf: anker 0x180ae80 moet 22 geven.
# De CIFS-mount is vandaag wisselvallig (een keer "IO is still pending on closed socket"), dus: het
# bestand wordt met herhaling naar /tmp gekopieerd en daarna alleen van /tmp gelezen.
set -u
NAS="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam"
G_ORIG="$NAS/g7/halo/game"
G=/tmp/io-brug-game.bin

G_VORIG=/tmp/io-brug-game.bin.vorig
if [ ! -s "$G" ]; then
  echo "--- de game naar /tmp kopiëren (met herhaling)"
  for poging in 1 2 3 4 5; do
    if timeout 120 cp -f "$G_ORIG" "$G.part" 2>/dev/null && [ "$(stat -c %s "$G.part" 2>/dev/null)" = "33554432" ]; then
      mv -f "$G.part" "$G"; echo "    kopie gelukt bij poging $poging"; break
    fi
    echo "    poging $poging mislukt (CIFS)"; sleep 5
  done
fi
[ -s "$G" ] || { echo "*** GEEN KOPIE VAN DE GAME; ik stop"; exit 1; }

echo "############ IO-BRUG-ROUTE stap 1 (statisch, alleen lezen) $(date '+%F %H:%M:%S')"
echo "--- identiteit van de bron (de NAS zelf, eenvoudige stat)"
timeout 60 stat -c '    %n  maat=%s  mtime=%y' "$G_ORIG" "$NAS/g7/halo/game_original" "$NAS/g7/halo/halo.so" 2>&1
echo "--- sha256 van de kopie (dit is de binary waar alles hieronder op slaat)"
sha256sum "$G"
ls -la "$G"
echo "--- NAS-vingerafdruk VOOR:"
timeout 60 find "$NAS/g7" -maxdepth 3 -printf '%p %s %T@\n' 2>/dev/null | sort | md5sum | sed 's/^/    md5 van de g7-boom: /'

echo
echo "############ A. PROGRAMMAHEADERS (klopt vaddr - offset = 0x400000?)"
readelf -hlW "$G" 2>/dev/null | head -34

echo
echo "############ B. DE GETTER 0x61e890 (de schakelaartafel) -- volledig"
objdump -D -b binary -m i386:x86-64 --adjust-vma=0x400000 \
  --start-address=0x61e890 --stop-address=0x61eaa0 "$G" 2>&1 | tail -n +8

echo
echo "############ C. DE TAK <= 0x1ff (0x61e930) -- welk bord/kanaal is dat?"
objdump -D -b binary -m i386:x86-64 --adjust-vma=0x400000 \
  --start-address=0x61e920 --stop-address=0x61e9d0 "$G" 2>&1 | tail -n +8

echo
echo "############ D. DE ZUSTERGETTER 0x61ee70 (soort==0 = ANALOOG KANAAL) -- volledig"
objdump -D -b binary -m i386:x86-64 --adjust-vma=0x400000 \
  --start-address=0x61ee70 --stop-address=0x61f060 "$G" 2>&1 | tail -n +8

echo
echo "############ E. ROND 0x61ee68 (de negatieve tak van de getter)"
objdump -D -b binary -m i386:x86-64 --adjust-vma=0x400000 \
  --start-address=0x61ee40 --stop-address=0x61ee90 "$G" 2>&1 | tail -n +8

echo
echo "############ F. SCANNER, EERST GEKALIBREERD (RIP-relatief, HERSTELDE formule)"
python3 - "$G" <<'PY'
import struct, sys
d = open(sys.argv[1],'rb').read()
print("    binary: %d bytes" % len(d))
PATS = [bytes.fromhex(x) for x in (
 '488d05','488d0d','488d15','488d1d','488d25','488d2d','488d35','488d3d',
 '0f2805','0f2905','f30f1005','f30f1105','8b05','8905','488b05','488905',
 '833d','803d','0fb605','0fbe05','c705','c605','8d05')]
def rip_doelen(doel):
    hits=[]
    for pat in PATS:
        i = d.find(pat)
        while i >= 0:
            disp = struct.unpack_from('<i', d, i+len(pat))[0]
            tgt = (i + 0x400000) + len(pat) + 4 + disp
            if tgt == doel: hits.append(hex(i+0x400000))
            i = d.find(pat, i+1)
    return hits
print("    KALIBRATIE anker 0x180ae80 : %d verwijzingen (verwacht 22)" % len(rip_doelen(0x180ae80)))
print("    KALIBRATIE anker+0x48      : %d" % len(rip_doelen(0x180aec8)))
print("    KALIBRATIE hulp+0xaea0     : %d" % len(rip_doelen(0x1812d20)))
for naam, adr in (("masker A 0x1766f40", 0x1766f40), ("masker B 0x181d8a0", 0x181d8a0),
                  ("aim x 0x1f392a0", 0x1f392a0), ("poortbyte 0x1aa37c0", 0x1aa37c0)):
    hits = rip_doelen(adr)
    print("    %-24s : %d verwijzingen  %s" % (naam, len(hits), " ".join(hits[:12])))
    for w in (4,8):
        n = 0; i = d.find(adr.to_bytes(w,'little'))
        while i >= 0: n += 1; i = d.find(adr.to_bytes(w,'little'), i+1)
        print("        absolute literaal %d-byte: %d" % (w, n))
print("    --- immediate 0xaea0 (32-bit LE) in de hele binary (max 12):")
i = d.find(struct.pack('<I', 0xaea0)); n=0
while i >= 0 and n < 12:
    print("        bestandsafstand 0x%x (= vaddr 0x%x)" % (i, i+0x400000)); n+=1
    i = d.find(struct.pack('<I', 0xaea0), i+1)
print("    --- ALGEMENE disp32-scan voor de maskers (geen opcode-aanname). LET OP: traag (31M posities).")
def alle_disp(T):
    basis = 0x400000
    un = struct.Struct('<i').unpack_from
    uit = []
    for p in range(len(d)-3):
        if un(d, p)[0] == T - (p + 4 + basis):
            uit.append(hex(p+basis))
    return uit
for naam, T in (("masker A 0x1766f40",0x1766f40), ("masker B 0x181d8a0",0x181d8a0)):
    r = alle_disp(T)
    print("        %-22s : %d plekken  %s" % (naam, len(r), " ".join(r[:10])))
PY

echo
echo "############ G. DE STATISCHE INHOUD OP DE TWEE MASKER-ADRESSEN"
python3 - "$G" <<'PY'
import struct, sys
d=open(sys.argv[1],'rb').read()
def toon(adres, n=64):
    off = adres - 0x400000
    print("    %#x (bestandsafstand %#x):" % (adres, off))
    if off < 0 or off+n > len(d): print("        BUITEN HET BESTAND"); return
    b = d[off:off+n]
    for r in range(0, n, 16):
        hexs = " ".join("%02x"%x for x in b[r:r+16])
        u32  = " ".join("%08x"%struct.unpack_from('<I',b,r+k)[0] for k in range(0,16,4))
        print("        +%02x  %-47s  %s" % (r, hexs, u32))
for a in (0x1766f40, 0x181d8a0): toon(a)
PY

echo
echo "############ H. DE SCHAKELAARTABEL STATISCH: bord 0, 32 vakken op 0x180ae80 EN op 0x1812d20"
python3 - "$G" <<'PY'
import struct, sys
d=open(sys.argv[1],'rb').read()
def vakken(basis, label, n=32, stride=0x50):
    print("    --- %s = %#x" % (label, basis))
    print("        vak  adres       [0]  [1]  [2]  [3]  [4]  [5]  [8]")
    for s in range(n):
        a = basis + s*stride; off = a - 0x400000
        if off < 0 or off+0x50 > len(d): print("        %3d  %#x  BUITEN" % (s,a)); continue
        w = struct.unpack_from('<16I', d, off)
        print("        %3d  %#x  %4d %4d %4d %4d %4d %4d %4d" % (s, a, w[0], w[1], w[2], w[3], w[4], w[5], w[8]))
vakken(0x180ae80, "getter-tafel bord 0")
vakken(0x1812d20, "hulp-tafel bord 0 (0x180ae80+0xaea0)")
PY

echo
echo "--- NAS-vingerafdruk NA:"
timeout 60 find "$NAS/g7" -maxdepth 3 -printf '%p %s %T@\n' 2>/dev/null | sort | md5sum | sed 's/^/    md5 van de g7-boom: /'
echo "############ KLAAR $(date '+%F %H:%M:%S')"
