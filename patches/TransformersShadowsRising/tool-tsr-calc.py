#!/usr/bin/env python3
"""Rekent de 'transformed' bytes van het IOBoard-draaiboek uit de exacte code in Shell.exe na.

forward (0x43aee0): v = x ^ 0x92; v = ROL1^7(v); v ^= 0x35; v = ROR1^6(v); v ^= 0xA7; v = ROL1^3(v); v ^= k
inverse (0x43af40): v = x ^ k;    v = ROR1^3(v); v ^= 0xA7; v = ROL1^6(v); v ^= 0x35; v = ROR1^7(v); v ^= 0x92
"""

def rol(b, n):
    n %= 8
    return ((b << n) | (b >> (8 - n))) & 0xFF


def ror(b, n):
    n %= 8
    return ((b >> n) | (b << (8 - n))) & 0xFF


def forward(x, k):
    v = (x ^ 0x92) & 0xFF
    v = rol(v, 7)          # lus met dl=7 -> 7 x ROL1
    v ^= 0x35
    v = ror(v, 6)          # lus met dl=6 -> 6 x ROR1
    v ^= 0xA7
    v = rol(v, 3)          # lus met dl=3 -> 3 x ROL1
    v ^= k
    return v & 0xFF


def inverse(x, k):
    v = (x ^ k) & 0xFF
    v = ror(v, 3)
    v ^= 0xA7
    v = rol(v, 6)
    v ^= 0x35
    v = ror(v, 7)
    v ^= 0x92
    return v & 0xFF


# zelftest: inverse moet forward ongedaan maken voor alle x en een paar k
bad = 0
for k in (0x00, 0x09, 0x3B, 0x46, 0x69, 0x74, 0xE9):
    for x in range(256):
        if inverse(forward(x, k), k) != x:
            bad += 1
print("zelftest inverse(forward(x,k),k)==x : %s" % ("OK" if bad == 0 else "FOUT (%d)" % bad))

print()
print("IDREQUEST1  verzonden door shell: 00 %02X %02X   (report[1]=0x13fcffa, report[2]=0x13fd02b)" % (0x74, 0xE0))
print("IDREQUEST2  verzonden door shell: 00 %02X %02X" % (0x09, 0xE1))
print("ENABLE KEYPAD verzonden:         00 %02X %02X" % (0x00, 0x82))
print("VERSION (onbereikbaar):          00 %02X %02X" % (0x00, 0x96))

print()
# UNLOCK1: 0x43b76d, esi=0x13fcff2 (staat op 0x74), eerst inverse met al=0x69, dan met al=0x46
v = 0x74
v = inverse(v, 0x69)
v = inverse(v, 0x46)
print("IOBOARD_UNLOCK1: 0x13fcff2 = 0x74 -> inverse(0x69) -> inverse(0x46) = 0x%02X" % v)
print("   verzonden: 00 %02X F0" % v)
unlock1 = v

# UNLOCK2: 0x43b803, esi=0x13fd029 (staat op 0x09), inverse met al=0xE9, dan met al=0x3B
u = 0x09
u = inverse(u, 0xE9)
u = inverse(u, 0x3B)
print("COMMAND_UNLOCK2: 0x13fd029 = 0x09 -> inverse(0xE9) -> inverse(0x3B) = 0x%02X" % u)
print("   verzonden: 00 %02X F1" % u)

print()
print("Terugcontrole (juiste volgorde: de code doet eerst inverse(0x69), dan inverse(0x46),")
print("dus omkeren betekent eerst forward(0x46), dan forward(0x69)):")
a = forward(unlock1, 0x46)
b = forward(a, 0x69)
print("  forward(0x%02X, 0x46) = 0x%02X ; forward(0x%02X, 0x69) = 0x%02X  (moet 0x74 zijn) -> %s"
      % (unlock1, a, a, b, "OK" if b == 0x74 else "FOUT"))
c = forward(u, 0x3B)
d = forward(c, 0xE9)
print("  forward(0x%02X, 0x3B) = 0x%02X ; forward(0x%02X, 0xE9) = 0x%02X  (moet 0x09 zijn) -> %s"
      % (u, c, c, d, "OK" if d == 0x09 else "FOUT"))

