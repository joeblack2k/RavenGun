#!/usr/bin/env python3
"""Read-only: why did ONE run reach the attract screen and the others not?

Compares the UE4 log of the single successful run against the failures, ignoring
timestamps and frame counters, and reports the first real divergence plus what each
side was doing around it.

Usage: python logdiff.py <success.log> <failure.log> [<failure2.log> ...]
"""
import re
import sys

TS = re.compile(r"^\[[\d.\-:\[\] ]+\]")
FRAME = re.compile(r"\[[ \d]+\]")


def norm(line):
    l = TS.sub("", line).strip()
    l = FRAME.sub("", l)
    return l


def load(p):
    out = []
    with open(p, encoding="utf-8", errors="replace") as f:
        for raw in f:
            n = norm(raw)
            if n:
                out.append((raw.rstrip("\n"), n))
    return out


def key(n):
    """a loose key: drop numbers and paths so real content lines line up"""
    k = re.sub(r"\d+", "#", n)
    k = re.sub(r"['\"].*?['\"]", "'X'", k)
    return k


def main():
    succ = load(sys.argv[1])
    print("### SUCCES: %s  (%d regels)" % (sys.argv[1], len(succ)))
    sk = [key(n) for _, n in succ]
    for failp in sys.argv[2:]:
        fail = load(failp)
        fk = [key(n) for _, n in fail]
        print()
        print("=" * 74)
        print("### VERGELIJK met %s  (%d regels)" % (failp, len(fail)))
        # first index where the keyed content differs
        i = 0
        while i < min(len(sk), len(fk)) and sk[i] == fk[i]:
            i += 1
        print("   eerste verschil op regel %d" % (i + 1))
        print("   -- laatste 4 GELIJKE regels (de laatste die beide deden):")
        for raw, n in fail[max(0, i - 4):i]:
            print("      | %s" % n[:150])
        print("   -- de MISLUKTE run doet daarna nog %d regels; de laatste 10:" % (len(fail) - i))
        for raw, n in fail[i:][-10:]:
            print("      | %s" % n[:150])
        print("   -- en de GESLAAGDE run op dat punt (de volgende 14 regels):")
        for raw, n in succ[i:i + 14]:
            print("      > %s" % n[:150])
        # what does the success have that the failure never has?
        fset = set(fk)
        uniq = []
        for j, k in enumerate(sk):
            if k not in fset:
                uniq.append(succ[j][1])
        print("   -- %d regels die de GESLAAGDE run wel heeft en de mislukte niet (eerste 14):" % len(uniq))
        for u in uniq[:14]:
            print("      + %s" % u[:150])


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(errors="replace")
    except Exception:
        pass
    main()
