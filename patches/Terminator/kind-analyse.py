#!/usr/bin/env python3
# kind-analyse.py - READ-ONLY. Leest de logboeken van libravengun-fork-log.so (+ de tijdstempels uit
# libravengun-ptrace-log.so) en beantwoordt de vraag: WERKT de gast of SPINT hij?
#
# Gebruik:
#   python3 kind-analyse.py <map met B.fork.log en B.ptrace.log>
#   python3 kind-analyse.py /tmp/ts-hasp            (of welke map dan ook)
#
# De vraag is niet triviaal: 392 kinderen in 4 seconden kan een werkerpool zijn die per taak een proces
# opstart, of een lus die zichzelf opjaagt. De criteria die dat onderscheiden staan in de uitvoer,
# zodat je de conclusie kunt narekenen en niet alleen hoeft te geloven.
import sys, os, re, collections, statistics

def lees(pad):
    try:
        return open(pad, "r", errors="replace").read().splitlines()
    except OSError:
        return []

def velden(regel, sleutel):
    """Haalt een veld op in BEIDE notaties die onze bibliotheken gebruiken: de fork-waarnemer schrijft
       `pid=123` en de ptrace-waarnemer schrijft `pid 123`. Alleen op `=` matchen liet alle
       ptrace-regels naar de sleutel None vallen, waardoor 388 aanroepen onder één pid leken te
       hangen. Dat is precies de fout die deze functie nu afvangt."""
    m = re.search(r"\b%s[=\s]+(-?\d+)" % sleutel, regel)
    return int(m.group(1)) if m else None

def tijd(regel):
    m = re.match(r"\s*\+?\s*([0-9]+\.[0-9]+)", regel)
    if m: return float(m.group(1))
    m = re.search(r"\+\s*([0-9]+\.[0-9]+)\s+pid", regel)
    return float(m.group(1)) if m else None

map_ = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ts-hasp"
fork_pad = os.path.join(map_, "B.fork.log")
ptrace_pad = os.path.join(map_, "B.ptrace.log")
F = lees(fork_pad); P = lees(ptrace_pad)
print("map        : %s" % map_)
print("fork-log   : %s (%d regels)" % (fork_pad, len(F)))
print("ptrace-log : %s (%d regels)" % (ptrace_pad, len(P)))
print()

# ---------------------------------------------------------------- fork-gebeurtenissen
gemaakt = []      # (t, kindpid)
opgeraapt = {}    # kindpid -> (t, gestopt?, code_of_signaal)
netjes = {}       # kindpid -> t  (via exit())
for r in F:
    t = tijd(r)
    if t is None: continue
    if "kind-gemaakt" in r:
        k = velden(r, "kind")
        if k is not None: gemaakt.append((t, k))
    elif "kind-opgeraapt" in r:
        k = velden(r, "kind")
        if k is None: continue
        if "GESTOPT door signaal" in r:
            s = re.search(r"signaal (\d+)", r)
            opgeraapt[k] = (t, True, int(s.group(1)) if s else -1)
        elif "geëindigd met code" in r or "geindigd met code" in r:
            s = re.search(r"code (\d+)", r)
            opgeraapt[k] = (t, False, int(s.group(1)) if s else -1)
        else:
            opgeraapt[k] = (t, None, -1)
    elif "einde exit-code" in r:
        netjes[velden(r, "pid")] = t

if not gemaakt:
    print("GEEN kind-gemaakt-regels. Is libravengun-fork-log.so wel voorgeladen?")
    print("Zonder deze bibliotheek valt er niets over werkerpool-vs-lus te zeggen.")
    sys.exit(2)

t_lo = min(t for t, _ in gemaakt); t_hi = max(t for t, _ in gemaakt)
span = max(t_hi - t_lo, 1e-6)
print("=== 1. hoeveel en hoe snel ===")
print("  kinderen gemaakt      : %d" % len(gemaakt))
print("  waarneemvenster       : %.3f s (eerste kind tot laatste kind)" % span)
print("  tempo                 : %.1f kinderen/s" % (len(gemaakt) / span))
print("  unieke kind-pids      : %d" % len({k for _, k in gemaakt}))

print()
print("=== 2. intervallen tussen opeenvolgende kinderen ===")
ts = sorted(t for t, _ in gemaakt)
iv = [b - a for a, b in zip(ts, ts[1:])]
if iv:
    print("  min / mediaan / max   : %.3f / %.3f / %.3f s" % (min(iv), statistics.median(iv), max(iv)))
    bak = collections.Counter()
    for d in iv:
        if   d < 0.001: bak["< 1 ms"] += 1
        elif d < 0.005: bak["1-5 ms"] += 1
        elif d < 0.020: bak["5-20 ms"] += 1
        elif d < 0.100: bak["20-100 ms"] += 1
        else:           bak[">= 100 ms"] += 1
    totaal = sum(bak.values())
    for k in ("< 1 ms", "1-5 ms", "5-20 ms", "20-100 ms", ">= 100 ms"):
        if bak[k]: print("    %-10s %5d  (%.0f%%)" % (k, bak[k], 100.0*bak[k]/totaal))

print()
print("=== 3. hoe lang leeft een kind, en hoeveel tegelijk? ===")
# De autoriteit is kind-opgeraapt (het echte wachtstatus-cijfer bij de ouder). Een kind dat via _exit()
# verdwijnt laat geen 'einde exit-code' achter, dus daar mag je NIET uit concluderen dat het gestopt is.
starttijd = {k: t for t, k in gemaakt}
paren = []
levens2 = []
gestopt = []
for k, (t_, is_gestopt, code) in opgeraapt.items():
    if k in starttijd and t_ >= starttijd[k]:
        levens2.append(t_ - starttijd[k])
        paren.append((starttijd[k], t_))
        if is_gestopt: gestopt.append((k, code))
if levens2:
    print("  kinderen opgeraapt door de ouder : %d van %d" % (len(opgeraapt), len(gemaakt)))
    print("  levensduur min/mediaan/max       : %.4f / %.4f / %.4f s"
          % (min(levens2), statistics.median(levens2), max(levens2)))
else:
    print("  GEEN enkel kind opgeraapt: de ouder wacht niet (of de waarnemer op waitpid is niet geladen)")
print("  waarvan GESTOPT (signaal)         : %d%s"
      % (len(gestopt), ("  <- " + ", ".join("pid %d signaal %d" % g for g in gestopt[:6])) if gestopt else ""))
print("  netjes via exit()                 : %d" % len(netjes))
print("  kinderen zonder opraap-regel      : %d van %d (die liepen nog, of de ouder raapte ze niet op)"
      % (len(gemaakt) - len(opgeraapt), len(gemaakt)))
# gelijktijdigheid
ev = []
for a, b in paren: ev.append((a, 1)); ev.append((b, -1))
ev.sort()
cur = mx = 0
for _, d in ev:
    cur += d; mx = max(mx, cur)
print("  maximale gelijktijdigheid         : %d kinderen  (1 = strikt na elkaar)" % mx)

print()
print("=== 4. wat deed elk kind? (uit het ptrace-logboek) ===")
per_kind = collections.Counter()
attach = detach = 0
for r in P:
    p = velden(r, "pid")
    if "PTRACE_ATTACH" in r:
        attach += 1; per_kind[p] += 1
    elif "PTRACE_DETACH" in r:
        detach += 1
    elif "PTRACE_TRACEME" in r:
        per_kind[p] += 1
print("  PTRACE_ATTACH / DETACH          : %d / %d" % (attach, detach))
print("  geslaagd / EPERM                : %d / %d" % (len([r for r in P if "ATTACH" in r and "ok" in r]),
                                                      len([r for r in P if "ATTACH" in r and "FOUT" in r])))
if per_kind:
    print("  ptrace-aanroepen per kind       : min %d, mediaan %.1f, max %d"
          % (min(per_kind.values()), statistics.median(list(per_kind.values())), max(per_kind.values())))

print()
print("=== 5. OORDEEL: werkerpool of lus? ===")
crit = []
if iv:
    crit.append(("intervallen < 1 ms domineren (>50%%)", 100.0*bak["< 1 ms"]/max(totaal,1) > 50))
    crit.append(("kinderen leven heel kort (< 5 ms mediaan)", bool(levens2) and statistics.median(levens2) < 0.005))
crit.append(("gelijktijdigheid is 1 (strikt na elkaar)", mx <= 1))
crit.append(("meer dan 100 kinderen", len(gemaakt) > 100))
for naam, ja in crit:
    print("  %-46s %s" % (naam, "JA" if ja else "nee"))
spint = sum(1 for _, ja in crit if ja) >= 3
print()
if spint:
    print("  ==> LUS (SPINT): veel kinderen, heel kort, strikt na elkaar. Dit is geen werkerpool die")
    print("      taken uitvoert maar een cyclus die zichzelf opjaagt. De volgende vraag is dan WAAROM")
    print("      die cyclus niet stopt -- en dat is een andere vraag dan 'de poort is dicht'.")
else:
    print("  ==> ziet er NIET uit als een pure lus. Kijk naar de gelijktijdigheid en de levensduur:")
    print("      een werkerpool heeft een GEBONDEN gelijktijdigheid (bv. 2-16) en kinderen die lang")
    print("      genoeg leven om werk te doen. Is de gelijktijdigheid onbegrensd of de levensduur")
    print("      ongeveer nul, dan is het alsnog een lus en moet criterium 1-3 opnieuw bekeken worden.")
    print()
    print("  LET OP: dit is een HINT, geen bewijs. Het onderscheid werkerpool/lus is met alleen")
    print("  fork-tijden niet hard te maken; het is een richting voor de volgende meting.")
