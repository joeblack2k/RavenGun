#!/usr/bin/env python3
"""Compare the FMOD names the game resolves (from its own internal name table in
`game`) with tools/fmod-stub/symbols.txt. Read-only."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

GAME = (sys.argv[1] if len(sys.argv) > 1
        else r"Z:\Teknoparrot\Halo Fireteam\g7\halo") + "/game"
SYMS = (sys.argv[2] if len(sys.argv) > 2
        else os.path.join(os.path.dirname(os.path.abspath(__file__)), "deck", "symbols.txt"))

START, END = 0x1b492c8, 0x1b54000
with open(GAME, "rb") as f:
    f.seek(START)
    d = f.read(END - START)

names, i = [], 0
while i < len(d):
    j = d.find(b"\0", i)
    if j < 0:
        break
    s = d[i:j]
    if s:
        names.append(s.decode("ascii", "replace"))
    i = j + 1

game_fmod = []
for n in names:
    if n.startswith(("_ZNK4FMOD", "_ZN4FMOD", "FMOD_")):
        game_fmod.append(n)
# stop at the first name that is clearly not part of the run any more
stub = [l.strip() for l in open(SYMS) if l.strip()]

# also collect the version tag that follows each FMOD name
vers = {}
for k, n in enumerate(names):
    if n.startswith(("_ZNK4FMOD", "_ZN4FMOD", "FMOD_")) and k + 1 < len(names):
        vers[n] = names[k + 1]

print("game table FMOD-ish names : %d" % len(game_fmod))
print("stub symbols.txt          : %d" % len(stub))
sg, ss = set(game_fmod), set(stub)
print("in game but NOT in stub   : %d  %s" % (len(sg - ss), sorted(sg - ss)))
print("in stub but NOT in game   : %d  %s" % (len(ss - sg), sorted(ss - sg)))
print("identical sets            : %s" % (sg == ss))
print()
print("version tag the game pairs with each FMOD name (sample):")
for n in sorted(sg):
    print("   %-30s -> %s" % (vers.get(n, "?"), n[:70]))
