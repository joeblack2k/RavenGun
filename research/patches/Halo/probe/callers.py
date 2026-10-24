#!/usr/bin/env python3
# Wie roept de twee tafel-getters aan, en met WELK vaknummer?
#   0x61e890 = schakelaar-getter (soort 1/2), 0x61ee70 = analoog-getter (soort 0).
# Dit is de bench-vrije weg naar de slotnummers. Alleen directe e8-calls en absolute 8-byte
# functie-POINTERS (die zijn er ook: de laderbouwer vond eerder dat 0xdd57e0 alleen via een pointer
# wordt aangeroepen). Instrument eerst gekalibreerd op een bekend antwoord.
import struct, sys
G = sys.argv[1] if len(sys.argv) > 1 else '/tmp/io-brug-game.bin'
d = open(G, 'rb').read()
VA = 0x400000

def directe_calls(doel):
    """alle e8 rel32 die naar doel springen"""
    uit = []
    p = d.find(b'\xe8')
    while p >= 0:
        if p + 5 <= len(d):
            rel = struct.unpack_from('<i', d, p+1)[0]
            if (p + VA) + 5 + rel == doel:
                uit.append(p + VA)
        p = d.find(b'\xe8', p+1)
    return uit

def verwijzingen(doel):
    """alle plekken waar het 8-byte adres als getal staat (functiepointer/table)"""
    uit = []
    b = struct.pack('<Q', doel)
    p = d.find(b)
    while p >= 0:
        uit.append(p + VA)
        p = d.find(b, p+1)
    return uit

def rip_leas(doel):
    """alle RIP-relatieve verwijzingen (elke vorm) naar het adres als DATA"""
    uit = []
    for pat in (b'\x48\x8d\x05', b'\x48\x8d\x0d', b'\x48\x8d\x15', b'\x48\x8d\x1d',
                b'\x48\x8b\x05', b'\x8b\x05', b'\x89\x05'):
        p = d.find(pat)
        while p >= 0:
            disp = struct.unpack_from('<i', d, p+len(pat))[0]
            if (p+VA) + len(pat) + 4 + disp == doel:
                uit.append((p + VA, pat.hex()))
            p = d.find(pat, p+1)
    return uit

KAL = 0x61e890
print("KALIBRATIE: directe calls naar 0x61e890 (schakelaar-getter): %d" % len(directe_calls(KAL)))

for naam, doel in (("SCHAKELAAR-getter 0x61e890", 0x61e890), ("ANALOOG-getter 0x61ee70", 0x61ee70)):
    print()
    print("=" * 78)
    print("%s" % naam)
    c = directe_calls(doel)
    print("  directe e8-calls: %d   %s" % (len(c), " ".join(hex(x) for x in c)))
    v = verwijzingen(doel)
    print("  8-byte pointers : %d   %s" % (len(v), " ".join(hex(x) for x in v)))
    r = rip_leas(doel)
    print("  rip-verwijzingen: %d   %s" % (len(r), " ".join(hex(x[0]) for x in r)))
