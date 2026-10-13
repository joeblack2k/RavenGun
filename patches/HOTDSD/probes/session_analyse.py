#!/usr/bin/env python3
"""Read-only analysis of the six HOTDSD UE4 sessions (fetched from the prefix).

Answers, per session: where did it end, and did it end in a crash, a clean stop, or an
abrupt kill?  Plus the aimeId context, and the content paths the engine asks for.

Writes nothing.  Usage: python session_analyse.py <dir-with-logs>
"""
import os
import re
import sys

D = sys.argv[1] if len(sys.argv) > 1 else "."

FILES = [
    ("sessie 5 (de crashsessie, 08:52:40)", "sessie5-08.55.41.log"),
    ("crashmap-kopie van sessie 5", "crashsessie-Hodzero.log"),
    ("sessie 4 (08:47:52)", "sessie4-08.52.40.log"),
    ("sessie 6 = de zevenminutensessie", "zevenmin-Hodzero.log"),
]

STOP = ["Log file closed", "Critical error", "Fatal error", "RequestExit",
        "Executing StaticShutdownAfterError", "LogExit"]


def load(name):
    p = os.path.join(D, name)
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        return f.read().replace("\r\n", "\n").split("\n")


def stamps(lines):
    """timestamps like [2026.10.11-06.52.46:461]"""
    out = []
    for i, l in enumerate(lines):
        m = re.search(r"\[(\d{4}\.\d\d\.\d\d)-(\d\d\.\d\d\.\d\d):(\d\d\d)\]", l)
        if m:
            out.append((i, m.group(2)))
    return out


def main():
    for label, fn in FILES:
        p = os.path.join(D, fn)
        if not os.path.exists(p):
            print("### %s -- FILE MISSING (%s)" % (label, fn))
            continue
        lines = load(fn)
        body = [l for l in lines if l.strip()]
        print("=" * 78)
        print("### %s   [%s]" % (label, fn))
        print("    regels=%d  niet-leeg=%d  bytes=%d" % (len(lines), len(body),
                                                         os.path.getsize(p)))
        print("    open : %s" % body[0][:110])
        ts = stamps(lines)
        if ts:
            print("    eerste tijdstempel: %s   laatste: %s" % (ts[0][1], ts[-1][1]))
        print("    laatste regel: %s" % body[-1][:110])

        print("    -- afsluitmarkeringen --")
        found = False
        for s in STOP:
            hits = [i for i, l in enumerate(lines) if s in l]
            if hits:
                found = True
                print("       %-38s %d×  (regel %s)" % (s, len(hits), hits[-1] + 1))
        if not found:
            print("       GEEN van de afsluitmarkeringen aanwezig")

        print("    -- laatste 12 niet-lege regels --")
        for l in body[-12:]:
            print("       %s" % l[:150])

        print("    -- fouten in deze sessie (uniek) --")
        errs = {}
        for l in lines:
            m = re.search(r"(Log\w+): (Error|Warning): (.{0,90})", l)
            if m:
                k = "%s: %s: %s" % (m.group(1), m.group(2), m.group(3))
                errs[k] = errs.get(k, 0) + 1
        for k, v in sorted(errs.items(), key=lambda x: -x[1])[:12]:
            print("       %3d×  %s" % (v, k))
        print("    -- LogAMDaemon in deze sessie --")
        for i, l in enumerate(lines):
            if "LogAMDaemon" in l:
                print("       r%-6d %s" % (i + 1, l[:150]))
        print()

    # aimeId context, from the seven-minute session
    print("=" * 78)
    print("### DE aimeId-REGEL MET CONTEXT (zevenminutensessie)")
    lines = load("zevenmin-Hodzero.log")
    for i, l in enumerate(lines):
        if "LogAMDaemon" in l:
            lo, hi = max(0, i - 12), min(len(lines), i + 13)
            print("---- context rond regel %d ----" % (i + 1))
            for j in range(lo, hi):
                mark = ">>" if j == i else "  "
                if lines[j].strip():
                    print("   %s r%-6d %s" % (mark, j + 1, lines[j][:160]))

    # the content paths the engine asks for (the case question)
    print()
    print("=" * 78)
    print("### DE PADEN DIE DE ENGINE VRAAGT (hoofdlettergebruik)")
    seen = {}
    for fn in ("zevenmin-Hodzero.log", "crashsessie-Hodzero.log"):
        for l in load(fn):
            for m in re.finditer(r"\.\./\.\./\.\./[A-Za-z0-9_./\-]+", l):
                seen[m.group()] = seen.get(m.group(), 0) + 1
    for k in sorted(seen, key=lambda x: -seen[x]):
        print("   %5d×  %s" % (seen[k], k))
    print()
    print("   -- .usm- en fontverwijzingen (unieke paden) --")
    usm = set()
    for fn in ("zevenmin-Hodzero.log", "crashsessie-Hodzero.log"):
        for l in load(fn):
            for m in re.finditer(r"[A-Za-z0-9_./\-]*\.(?:usm|ufont|uasset|pak)", l):
                usm.add(m.group())
    for u in sorted(usm)[:40]:
        print("      %s" % u)


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(errors="replace")
    except Exception:
        pass
    main()
