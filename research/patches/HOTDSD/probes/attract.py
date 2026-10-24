#!/usr/bin/env python3
"""Read-only: the attract window of a UE4 log, with a tolerant timestamp parser.

UE4 writes [YYYY.MM.DD-HH.MM.SS:mmm] but a few lines carry no milliseconds, which is
what tripped the strict parser.  The log clock is UTC; the Deck is UTC+2.

Usage: python attract.py <log>
"""
import re
import sys

TS = re.compile(r"\[(\d{4})\.(\d\d)\.(\d\d)-(\d\d)\.(\d\d)\.(\d\d)(?::(\d\d\d))?\]")


def secs(m):
    h, mi, s = int(m.group(4)), int(m.group(5)), int(m.group(6))
    ms = int(m.group(7)) if m.group(7) else 0
    return h * 3600 + mi * 60 + s + ms / 1000.0


def local(x):
    x = x + 7200
    return "%02d:%02d:%02d" % (int(x) // 3600 % 24, int(x) // 60 % 60, int(x) % 60)


def main():
    t = open(sys.argv[1], encoding="utf-8", errors="replace").read()
    lines = [l for l in t.split("\n") if l.strip()]
    stamps = [(m, secs(m)) for m in TS.finditer(t)]
    if not stamps:
        print("geen tijdstempels"); return
    first, last = stamps[0][1], stamps[-1][1]
    print("   UE4-logboek   : %d B, %d regels" % (len(t), len(lines)))
    print("   Error-regels  : %d   RHI: %d   MatchState: %d   LoadMap: %d" % (
        len(re.findall(r"Error:", t)), len(re.findall(r"LogD3D11RHI|LogRHI", t)),
        len(re.findall(r"Match State Changed", t)), len(re.findall(r"to LoadMap", t))))
    print("   eerste regel  : %s (lokaal %s)" % (lines[0][:90], local(first)))
    print("   laatste regel : %s (lokaal %s)" % (lines[-1][:110], local(last)))
    print("   totale duur   : %.0f s" % (last - first))
    # every map load, in order
    print("   -- de maps die geladen zijn:")
    for m in TS.finditer(t):
        rest = t[m.end():t.find("\n", m.end())]
        lm = re.search(r"to LoadMap\(([^)]*)\)", rest)
        if lm:
            print("      %s (lokaal %s)  %s" % (m.group(0)[1:-1], local(secs(m)), lm.group(1)))
        if "PL_Advertise" in rest and "LoadMap" not in rest:
            print("      %s (lokaal %s)  *PL_Advertise genoemd*" % (m.group(0)[1:-1], local(secs(m))))
    adv = [secs(m) for m in TS.finditer(t)
           if "PL_Advertise" in t[m.end():t.find("\n", m.end())]]
    inip = [secs(m) for m in TS.finditer(t)
            if "PL_InitializeTest" in t[m.end():t.find("\n", m.end())]]
    print()
    if adv:
        a = min(adv)
        print("   **ATTRACT-VENSTER: %.0f s**   (PL_Advertise het eerst om %s lokaal, laatste logregel %s lokaal)"
              % (last - a, local(a), local(last)))
        print("      de lat is 300 s -> %s" % ("GEHAALD" if (last - a) >= 300 else "NIET gehaald"))
    else:
        print("   **ATTRACT-VENSTER: 0 s** (PL_Advertise niet geladen)")
        if inip:
            print("      wel PL_InitializeTest om %s lokaal" % local(min(inip)))


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(errors="replace")
    except Exception:
        pass
    main()
