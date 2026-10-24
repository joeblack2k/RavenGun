#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Correct the wrong Jurassic Park claim in the two documents (ASCII-only literals).

My earlier check used `readelf --dyn-syms`, which returns nothing on these files because
`game` has ZERO section headers. Read through PT_DYNAMIC, Jurassic Park's dynsym has 29
names containing `hasp_` (the game's own drop-in.ini says 30), all defined.
"""
import io

P1 = r"re\halo_probe\NATIVE-RECIPE-PATCH.md"
P2 = r"re\halo_probe\HASP-INJECTIE.md"

OUD1 = ("  in het hele bestand voor. Hetzelfde geldt voor Jurassic Park's `game` (ook nul hasp-namen), dus\n"
        "  dit is een familie-eigenschap en geen Halo-eigenaardigheid.")
NIEUW1 = ("  in het hele bestand voor.\n"
          "  **Let op: dat is niet voor de hele familie zo.** Jurassic Park's `game` heeft **29 namen met\n"
          "  `hasp_`** in zijn dynsym (de drop-in van die game zegt 30), allemaal gedefinieerd met een\n"
          "  absolute `st_value` rond `0x0852xxxx` \u2014 gemeten met `probe/dynsym_probe.py`, dat de\n"
          "  symbolentabel via `PT_DYNAMIC` leest. Een eerdere versie van dit document beweerde het\n"
          "  omgekeerde; die fout kwam van `readelf --dyn-syms`, dat op deze bestanden **niets** teruggeeft\n"
          "  omdat `game` nul sectiekoppen heeft. Zie `DROPIN-GELIJKSTELLING.md` \u00a72.")

OUD2 = ("voor Jurassic Park's `game` ook niet (gemeten: ook nul hasp-namen in de dynsym). De conclusie in")
NIEUW2 = ("voor Jurassic Park's `game` **wel**: die heeft 29 namen met `hasp_` in zijn dynsym (zie\n"
          "`DROPIN-GELIJKSTELLING.md` \u00a72 \u2014 een eerdere versie van dit document zei het omgekeerde, op\n"
          "basis van `readelf --dyn-syms`, dat op deze bestanden niets teruggeeft omdat `game` nul\n"
          "sectiekoppen heeft). De conclusie in")

for pad, oud, nieuw in ((P1, OUD1, NIEUW1), (P2, OUD2, NIEUW2)):
    s = io.open(pad, encoding="utf-8", errors="replace").read()
    if oud not in s:
        print("  NIET GEVONDEN in %s" % pad)
        continue
    io.open(pad, "w", encoding="utf-8", newline="\n").write(s.replace(oud, nieuw, 1))
    print("  gecorrigeerd: %s" % pad)
