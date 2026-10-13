#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Het eigen startscript van een spel LEZEN (game.bat, run_game.sh): wat doet de kast voordat het spel draait?

Een arcadespel heeft vaak een eigen startscript. Dat zet omgevingsvariabelen, kiest een werkmap, start soms eerst een
ander programma (een hardwarecontrole, een daemon) en geeft het spel argumenten mee. Onze launcher start het spel
zelf, met onze lader en kern, en sloeg dat allemaal over.

Dit script VOERT HET STARTSCRIPT NIET UIT. Dat kan niet: het script start de exe rechtstreeks (dus zonder onze lader
en kern), en het doet dingen die alleen op de kast horen (`rmdir /S /Q Y:\\`, `reboot`, `rm -rf` van de gebruikersmap,
een hardwarecontrole). Het leest het script als een beschrijving en haalt de feiten eruit:

  - welke omgevingsvariabelen het zet;
  - welke programma's het start, in welke orde, met welke argumenten;
  - in welke werkmap het spel start;
  - welke schijfletters het gebruikt;
  - en welke kastdingen erin staan (de reden dat het niet uitgevoerd wordt).

`vergelijk()` legt die feiten naast het startplan van de launcher, zodat `launch-direct.py --plan` zegt wat de kast
doet en wij niet. Wat ervan nodig is zet je per game in `patches/<Game>/game.ini` ([Env], [Launch] Arguments,
[Launch] WorkingDirectory, twee exe's); de launcher zelf blijft generiek.

Het is een lezer, geen vertolker: wat achter een voorwaarde staat (`if`, een blok tussen haakjes) wordt gemeld als
"voorwaardelijk", niet uitgerekend. Variabelen worden ingevuld voor zover het script ze zelf zet.

Gebruik: startscript.py SCRIPT [--root MAP] [--json]
"""
import json
import os
import re
import sys

MAX_DIEPTE = 6
MAX_BYTES = 1 << 20
BAT_KAST = ("rmdir", "rd", "del", "erase", "format", "shutdown", "taskkill", "reg")
SH_KAST = ("rm", "reboot", "shutdown", "halt", "modprobe", "rmmod", "mount", "umount", "ifconfig", "mkfs", "dd")
BAT_STIL = ("rem", "echo", "echo.", "goto", "exit", "setlocal", "endlocal", "timeout", "pause", "title", "cls", "mkdir", "md",
            "copy", "xcopy", "move", "ren", "popd", "color", "chcp", "ping", "sleep", "type")


def _lees(pad):
    with open(pad, "rb") as f:
        ruw = f.read(MAX_BYTES)
    for codec in ("utf-8-sig", "cp932", "latin-1"):
        try:
            return ruw.decode(codec).replace("\r", "")
        except UnicodeDecodeError:
            continue
    return ruw.decode("latin-1").replace("\r", "")


def _nieuw(rel):
    return {"script": rel, "files": [], "env": [], "runs": [], "drives": [], "cabinet": [], "unfollowed": [], "used": []}


class _Lezer:
    def __init__(self, wortel):
        self.wortel = os.path.abspath(wortel)
        self.f = None
        self.vars = {}          # naam in kleine letters -> waarde (zoals het script hem zet)
        self.cwd = None         # de map waartegen het script zijn paden oplost (absoluut, binnen de wortel) of None
        self.gekozen = False    # heeft het script zelf een werkmap gekozen (cd/pushd)? Anders is hij van de aanroeper
        self.stapel = []

    def rel(self, pad):
        return os.path.relpath(pad, self.wortel) if pad else None

    def binnen(self, pad):
        p = os.path.abspath(pad)
        return p == self.wortel or p.startswith(self.wortel + os.sep)

    def pad(self, tekst):
        """Een pad uit het script naar een pad op schijf, of None. Een schijfletter (X:\\App) wordt de wortel van de
        spelmap als de rest daar bestaat: op de kast is die letter de schijf met het spel."""
        t = tekst.strip().strip('"').replace("\\", "/")
        if not t:
            return None
        m = re.match(r"^([A-Za-z]):/?(.*)$", t)
        if m:
            kandidaat = os.path.normpath(os.path.join(self.wortel, m.group(2).lstrip("/")))
            return kandidaat if self.binnen(kandidaat) and os.path.exists(kandidaat) else None
        if t.startswith("/"):
            kandidaat = os.path.normpath(os.path.join(self.wortel, t.lstrip("/")))
            return kandidaat if os.path.exists(kandidaat) else None
        basis = self.cwd or self.wortel
        kandidaat = os.path.normpath(os.path.join(basis, t))
        return kandidaat if self.binnen(kandidaat) else None

    def letters(self, tekst):
        for m in re.finditer(r"(?<![A-Za-z0-9])([A-Za-z]):(?=[\\/]|\s|\"|$)", tekst):
            l = m.group(1).upper()
            if l not in ("C", "Z") and l not in self.f["drives"]:
                self.f["drives"].append(l)

    def werkmap(self):
        """De werkmap die het script zelf koos, of None: dan is het de map van waaruit de kast het script start."""
        return self.rel(self.cwd) if self.cwd and self.gekozen else None

    def feit(self, soort, voorwaardelijk, bestand, regel, **rest):
        d = dict(rest, conditional=bool(voorwaardelijk), file=self.rel(bestand), line=regel)
        self.f[soort].append(d)

    # ------------------------------------------------------------------------------------------------- .bat
    #
    # Een .bat is een programma: het kiest met `if exist` welke exe er is en met `if "%X%"=="y"` welke tak het neemt.
    # Wat uit de bestanden in de spelmap en uit de variabelen van het script zelf te BESLISSEN is, wordt beslist (die
    # tak wordt gevolgd, de andere niet). Wat van de kast afhangt (een errorlevel, een bestand op een andere schijf)
    # wordt niet geraden: wat in zo'n tak staat heet "voorwaardelijk".

    DYNAMISCH = ("date", "time", "errorlevel", "cd", "random", "cmdcmdline")

    def bat_vul(self, tekst, args):
        def var(m):
            naam, wijziging = m.group(1), m.group(2)
            if naam.lower() in self.DYNAMISCH:
                return "<%s>" % naam.upper()
            if naam.lower() not in self.vars:
                return ""                                  # in een .bat is een onbekende variabele leeg
            if naam.upper() not in self.f["used"]:
                self.f["used"].append(naam.upper())
            waarde = self.vars[naam.lower()]
            return "<%s>" % naam.upper() if wijziging else waarde     # %X:~2,6% en %X:a=b% rekenen we niet uit
        tekst = re.sub(r"%([1-9])", lambda m: args[int(m.group(1)) - 1] if int(m.group(1)) <= len(args) else "", tekst)
        return re.sub(r"%([A-Za-z_][A-Za-z0-9_]*)(:[^%]*)?%", var, tekst)

    def bat_voorwaarde(self, tekst, args):
        """(uitkomst, rest): True/False als de voorwaarde te beslissen is, None als ze van de kast afhangt."""
        t = re.sub(r"^/i\s+", "", tekst.strip(), flags=re.I)
        niet = bool(re.match(r"^not\s+", t, re.I))
        t = re.sub(r"^not\s+", "", t, flags=re.I)
        m = re.match(r'^exist\s+("[^"]*"|\S+)\s*(.*)$', t, re.I)
        if m:
            doel = self.bat_vul(m.group(1), args).strip('"')
            rest = m.group(2)
            if "<" in doel or not doel:
                return None, rest
            pad = self.pad(doel)
            if re.match(r"^[A-Za-z]:", doel) and pad is None:
                return None, rest                           # een andere schijf van de kast: niet te weten
            if not re.match(r"^[A-Za-z]:", doel) and not self.binnen(os.path.normpath(os.path.join(self.cwd or self.wortel, doel.replace("\\", "/")))):
                return None, rest
            uit = bool(pad and os.path.exists(pad))
            return (not uit if niet else uit), rest
        m = re.match(r'^("[^"]*"|[^\s=]+)\s*==\s*("[^"]*"|\S+)\s*(.*)$', t)
        if m:
            a, b = self.bat_vul(m.group(1), args), self.bat_vul(m.group(2), args)
            rest = m.group(3)
            if "<ERRORLEVEL>" in a + b or "<RANDOM>" in a + b:
                return None, rest
            uit = a.strip('"') == b.strip('"')
            return (not uit if niet else uit), rest
        return None, self._na_if("if " + tekst)

    def bat(self, bestand, args=(), diepte=0):
        if diepte > MAX_DIEPTE:
            self.f["unfollowed"].append(self.rel(bestand))
            return
        self.f["files"].append(self.rel(bestand))
        if self.cwd is None and diepte == 0:
            self.cwd = os.path.dirname(os.path.abspath(bestand))
        args = list(args)
        blokken = []          # per open blok: [toestand, genomen]; toestand: "ja" | "nee" | "?"; genomen: True | False | None
        naar = None           # een `goto` dat gevolgd wordt: overslaan tot dit label

        def toestand():
            t = [b[0] for b in blokken]
            return "nee" if "nee" in t else "?" if "?" in t else "ja"

        def open_blok(uitkomst):
            if toestand() == "nee":
                blokken.append(["nee", True])
            elif uitkomst is None:
                blokken.append(["?", None])
            else:
                blokken.append(["ja" if uitkomst else "nee", uitkomst])

        for nr, ruw in enumerate(_lees(bestand).split("\n"), 1):
            regel = ruw.strip().lstrip("@").strip()
            if naar is not None:
                if regel.lower().lstrip(":").strip() == naar and regel.startswith(":"):
                    naar = None
                continue
            if not regel or regel.startswith(":") or re.match(r"^rem(\s|$)", regel, re.I):
                continue
            # ") else if X (", ") else (" en ")"
            m = re.match(r"^\)\s*else\s+if\s+(.*?)\s*\($", regel, re.I)
            if m and blokken:
                boven = blokken.pop()
                if boven[1] is True:
                    blokken.append(["nee", True])
                elif boven[1] is None:
                    blokken.append(["?", None])
                else:
                    open_blok(self.bat_voorwaarde(m.group(1), args)[0])
                continue
            if re.match(r"^\)\s*else\s*\($", regel, re.I) and blokken:
                boven = blokken.pop()
                if toestand() == "nee" or boven[1] is True:
                    blokken.append(["nee", True])
                else:
                    blokken.append(["ja", True] if boven[1] is False else ["?", None])
                continue
            if regel.startswith(")"):
                if blokken:
                    blokken.pop()
                continue
            voor = None
            if re.match(r"^if\s", regel, re.I):
                uitkomst, rest = self.bat_voorwaarde(regel[2:], args)
                rest = rest.strip()
                if rest == "(" or (rest.endswith("(") and not rest[:-1].strip()):
                    open_blok(uitkomst)
                    continue
                if rest.endswith("("):                      # "if X cmd (" komt in de praktijk niet voor; als blok lezen
                    open_blok(uitkomst)
                    continue
                if uitkomst is False or not rest:
                    continue
                voor = uitkomst is None
                regel = rest
            elif regel.endswith("(") and re.match(r"^(for|\()", regel, re.I):
                blokken.append(["?", None])                 # een lus: wat erin staat hangt van de kast af
                continue
            st = toestand()
            if st == "nee":
                continue
            voor = bool(voor) or st == "?"
            woord = regel.split()[0].lower()
            if woord == "goto":
                if not voor:
                    doel = regel.split(None, 1)[1].strip().lstrip(":").lower() if len(regel.split()) > 1 else ""
                    if doel == "eof":
                        return
                    naar = doel
                continue
            if woord == "exit":
                if not voor:
                    return
                continue
            self.bat_regel(regel, voor, bestand, nr, args, diepte)

    @staticmethod
    def _na_if(regel):
        """Het commando achter een `if`-voorwaarde die we niet kunnen ontleden, of '' als er geen op dezelfde regel staat."""
        delen = regel.split()
        for i in range(2, len(delen)):
            t = delen[i].strip("(").lower()
            if t in ("set", "start", "call", "cd", "chdir", "pushd", "popd", "goto", "echo", "exit") + BAT_KAST \
                    or re.search(r"\.(exe|bat|cmd)\b", t):
                return " ".join(delen[i:])
        return ""

    def bat_regel(self, regel, voor, bestand, nr, args, diepte):
        self.letters(self.bat_vul(regel, args))
        # set NAAM=waarde, of set "NAAM=waarde" (alleen dan hoort het laatste aanhalingsteken bij de vorm en niet bij de waarde)
        m = re.match(r'^set\s+"([A-Za-z_][A-Za-z0-9_]*)=(.*)"\s*$', regel, re.I) or re.match(r"^set\s+([A-Za-z_][A-Za-z0-9_]*)=(.*?)\s*$", regel, re.I)
        if m and not re.match(r"^set\s+/", regel, re.I):
            waarde = self.bat_vul(m.group(2), args)
            # Een toekenning achter een voorwaarde die van de kast afhangt verandert niet wat we zeker weten: de
            # waarde die er al stond blijft gelden voor de rest van het script.
            if not voor or m.group(1).lower() not in self.vars:
                self.vars[m.group(1).lower()] = waarde
            self.feit("env", voor, bestand, nr, name=m.group(1), value=waarde)
            return
        woord = regel.split()[0].lower()
        if woord in ("cd", "chdir", "pushd"):
            doel = self.bat_vul(re.sub(r"^\S+\s*(/d\s+)?", "", regel, flags=re.I), args).strip().strip('"')
            if woord == "pushd":
                self.stapel.append((self.cwd, self.gekozen))
            if doel:
                self.cwd, self.gekozen = (self.pad(doel) if doel != "\\" else None), True
            return
        if woord == "popd":
            if self.stapel:
                self.cwd, self.gekozen = self.stapel.pop()
            return
        if re.match(r"^[A-Za-z]:$", regel):          # `c:` -- van schijf wisselen: de werkmap is daarna niet meer te volgen
            self.cwd, self.gekozen = None, True
            return
        if woord in BAT_KAST:
            self.feit("cabinet", voor, bestand, nr, command=self.bat_vul(regel, args))
            return
        if woord in BAT_STIL or woord in ("set", "if", "for", "else"):
            return
        # een programma of een ander script
        rest, wacht = regel, True
        if woord == "call":
            rest = regel.split(None, 1)[1] if len(regel.split(None, 1)) > 1 else ""
            if rest.startswith(":"):
                return                                      # een eigen subroutine van het script
        elif woord == "start":
            rest = regel.split(None, 1)[1] if len(regel.split(None, 1)) > 1 else ""
            wacht = False
            while True:
                m = re.match(r'^(/\S+|"[^"]*"(?=\s+"))\s+', rest)      # /wait, /min, en een titel ("") voor het echte pad
                if not m:
                    break
                wacht = wacht or m.group(1).lower() == "/wait"
                rest = rest[m.end():]
        rest = re.sub(r"^cmd(\.exe)?\s+/c\s+", "", rest.strip(), flags=re.I)        # `cmd /c X` is X
        rest = self.bat_vul(rest, args).strip()
        m = re.match(r'^"([^"]+)"\s*(.*)$', rest) or re.match(r"^(\S+)\s*(.*)$", rest)
        if not m:
            return
        prog, staart = m.group(1), m.group(2).strip()
        if re.search(r"\.(bat|cmd)$", prog, re.I):
            doel = self.pad(prog)
            if doel and os.path.isfile(doel) and self.rel(doel) not in self.f["files"]:
                self.bat(doel, win_split(staart), diepte + 1)
            else:
                self.f["unfollowed"].append(prog)
            return
        # Alles wat overblijft is een programma: met .exe, of een kale naam (sendFlash) die Windows zelf aanvult.
        pad = next((c for c in (self.pad(prog), self.pad(prog + ".exe")) if c and os.path.isfile(c)), None)
        self.feit("runs", voor, bestand, nr, exe=prog.replace("\\", "/").rsplit("/", 1)[-1], path=self.rel(pad) if pad else None,
                  args=staart, arglist=win_split(staart), wait=wacht, cwd=self.werkmap())

    # -------------------------------------------------------------------------------------------------- .sh

    def sh_vul(self, tekst):
        def var(m):
            naam = m.group(1) or m.group(2)
            if naam in self.vars:
                if naam not in self.f["used"]:
                    self.f["used"].append(naam)
                return self.vars[naam]
            return m.group(0)
        return re.sub(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}|\$([A-Za-z_][A-Za-z0-9_]*)", var, tekst)

    def sh(self, bestand, diepte=0):
        self.f["files"].append(self.rel(bestand))
        blok = 0
        for nr, ruw in enumerate(_lees(bestand).split("\n"), 1):
            regel = ruw.strip()
            if not regel or regel.startswith("#"):
                continue
            regel = re.sub(r"\s+#.*$", "", regel).rstrip(";").strip()
            if blok > 0:                                    # `222) reboot;` in een case: het commando achter het patroon
                m = re.match(r"^[^\s()]+\)\s*(.*)$", regel)
                if m:
                    regel = m.group(1).rstrip(";").strip()
            if not regel:
                continue
            eerste = regel.split()[0] if regel.split() else ""
            if eerste in ("fi", "esac", "done"):
                blok = max(0, blok - 1)
                continue
            voor = blok > 0
            if eerste in ("if", "case", "while", "for", "until"):
                blok += 1
                continue
            if eerste in ("then", "else", "elif", "do", "exit", "true", "false", "sleep", "echo", "return") or regel.endswith(")") and "(" not in regel:
                continue
            m = re.match(r"^(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)=(.*)$", regel)
            if m:
                waarde = self.sh_vul(m.group(2).strip().strip('"').strip("'"))
                self.vars[m.group(1)] = waarde
                self.feit("env", voor, bestand, nr, name=m.group(1), value=waarde, exported=regel.startswith("export"))
                continue
            vol = self.sh_vul(regel)
            delen = vol.split()
            if delen[0] == "cd":
                self.cwd, self.gekozen = (self.pad(delen[1]) if len(delen) > 1 else None), True
                continue
            if delen[0] in SH_KAST:
                self.feit("cabinet", voor, bestand, nr, command=vol)
                continue
            if delen[0] in ("sh", "bash", ".", "source") and len(delen) > 1:
                self.f["unfollowed"].append(delen[1])
                continue
            if "/" in delen[0]:
                pad = self.pad(delen[0])
                staart = vol[len(delen[0]):].strip()
                self.feit("runs", voor, bestand, nr, exe=delen[0].rsplit("/", 1)[-1], path=self.rel(pad) if pad else None,
                          args=staart, arglist=staart.split(), wait=not staart.endswith("&"), cwd=self.werkmap())


def win_split(tekst):
    """Een opdrachtregel-staart opdelen zoals een Windows-programma dat doet (zelfde regels als in launch-direct.py)."""
    uit, cur, in_q, bezig, i, n = [], [], False, False, 0, len(tekst)
    while i < n:
        c = tekst[i]
        if c == "\\":
            j = i
            while j < n and tekst[j] == "\\":
                j += 1
            aantal = j - i
            if j < n and tekst[j] == '"':
                cur.append("\\" * (aantal // 2))
                if aantal % 2:
                    cur.append('"')
                    j += 1
            else:
                cur.append("\\" * aantal)
            bezig, i = True, j
            continue
        if c == '"':
            if in_q and i + 1 < n and tekst[i + 1] == '"':
                cur.append('"')
                i += 2
                continue
            in_q, bezig = not in_q, True
        elif c in " \t" and not in_q:
            if bezig:
                uit.append("".join(cur))
                cur, bezig = [], False
        else:
            cur.append(c)
            bezig = True
        i += 1
    if bezig:
        uit.append("".join(cur))
    return uit


def lees(script, wortel=None):
    """De feiten uit een startscript (.bat/.cmd of .sh). `wortel` is de map van het spel (de view); paden in het
    resultaat zijn daar relatief aan. Voert niets uit en schrijft niets."""
    script = os.path.abspath(script)
    wortel = os.path.abspath(wortel or os.path.dirname(script))
    l = _Lezer(wortel)
    l.f = _nieuw(os.path.relpath(script, wortel))
    if script.lower().endswith((".bat", ".cmd")):
        l.bat(script)
    else:
        l.cwd = os.path.dirname(script)
        l.sh(script)
    l.f["drives"].sort()
    return l.f


def _kern(arg):
    """Waar je een argument aan herkent als de waarde uit een variabele komt: -launch="%X%" en -launch=cabinet zijn hetzelfde argument."""
    return arg.split("=", 1)[0] + "=" if "=" in arg else arg


def vergelijk(feiten, plan):
    """De feiten uit het startscript naast het startplan van de launcher. Geeft wat de kast doet en wij niet (en andersom)."""
    env = plan.get("env") or {}
    starts = plan.get("starts") or []
    onze = {s["name"].lower(): s for s in starts}
    uit = {"env_same": [], "env_differs": [], "env_missing": [], "game": None, "other_runs": [], "args_missing": [],
           "args_extra": [], "cwd_script": None, "cwd_ours": None, "cwd_same": None}
    laatste = {}
    for e in feiten["env"]:
        laatste[e["name"]] = e                       # de laatste toekenning telt
    for naam, e in laatste.items():
        # In een shellscript bereikt alleen een geexporteerde variabele het spel; de rest is van het script zelf.
        if e.get("exported") is False and naam not in ("PATH", "LD_LIBRARY_PATH", "LD_PRELOAD"):
            continue
        intern = naam.upper() in feiten["used"] or naam in feiten["used"]
        item = {"name": naam, "value": e["value"], "conditional": e["conditional"], "internal": intern}
        if naam not in env:
            uit["env_missing"].append(item)
        elif env[naam] == e["value"]:
            uit["env_same"].append(item)
        else:
            uit["env_differs"].append(dict(item, ours=env[naam]))
    spel = None
    for r in feiten["runs"]:
        s = onze.get(r["exe"].lower())
        if s and s["role"] == "main" and spel is None:
            spel = r
            ons = (s.get("args") or {}).get("list") or []
            onze_kernen = {_kern(a) for a in ons}
            hun_kernen = {_kern(a) for a in r["arglist"]}
            uit["args_missing"] = [a for a in r["arglist"] if a not in ons and _kern(a) not in onze_kernen]
            uit["args_extra"] = [a for a in ons if a not in r["arglist"] and _kern(a) not in hun_kernen]
            uit["game"] = {"exe": r["exe"], "args": r["args"], "conditional": r["conditional"], "file": r["file"], "line": r["line"]}
            uit["cwd_script"] = r["cwd"]
            # Onze werkmap: de map van de exe (zo start de lader het spel), of [Launch] WorkingDirectory uit game.ini.
            wd = env.get("TP_GAME_WORKING_DIRECTORY") or ""
            wortel = plan.get("_script_root") or ""
            onze_map = "/" + wd[2:].replace("\\", "/").strip("/") if re.match(r"^[Zz]:", wd) else (wd or os.path.dirname(s["exe"]))
            uit["cwd_ours"] = os.path.relpath(onze_map, wortel) if wortel and onze_map.startswith("/") else onze_map
            uit["cwd_same"] = None if r["cwd"] is None else os.path.normpath(r["cwd"]) == os.path.normpath(uit["cwd_ours"])
        else:
            ontbreekt = []
            if s:                                   # een start die wij ook doen (de tweede exe): mist hij argumenten?
                ons = (s.get("args") or {}).get("list") or []
                kernen = {_kern(a) for a in ons}
                ontbreekt = [a for a in r["arglist"] if a not in ons and _kern(a) not in kernen]
            uit["other_runs"].append({"exe": r["exe"], "args": r["args"], "conditional": r["conditional"],
                                      "before_game": spel is None, "ours": bool(s), "wait": r["wait"], "args_missing": ontbreekt})
    return uit


def regels(feiten, v=None):
    """Het startscript in gewone regels (voor --plan). Met `v` (van vergelijk) ook wat wij anders doen."""
    def vw(d):
        return " (voorwaardelijk)" if d.get("conditional") else ""
    uit = ["eigen startscript van het spel: %s%s -- gelezen, NIET uitgevoerd"
           % (feiten["script"], " (+ %d aangeroepen)" % (len(feiten["files"]) - 1) if len(feiten["files"]) > 1 else "")]
    if v is None:
        for e in feiten["env"]:
            uit.append("  zet %s=%s%s" % (e["name"], e["value"], vw(e)))
        for r in feiten["runs"]:
            uit.append("  start %s %s%s%s" % (r["exe"], r["args"], vw(r), " [werkmap %s]" % r["cwd"] if r["cwd"] else ""))
    else:
        ontbreekt = [e for e in v["env_missing"] if e["value"] != ""]
        if len(ontbreekt) <= 8:
            for e in ontbreekt:
                uit.append("  zet %s=%s%s -- WIJ GEVEN DIE NIET MEE%s"
                           % (e["name"], e["value"], vw(e), " (het script gebruikt hem ook zelf)" if e["internal"] else ""))
        else:
            # Een lang script zet tientallen hulpvariabelen. Welke daarvan het spel leest is niet te zien; de namen
            # staan hier, de waarden in --plan --json (start_script.compare.env_missing).
            alleen = [e["name"] for e in ontbreekt if not e["internal"]]
            zelf = [e["name"] for e in ontbreekt if e["internal"]]
            uit.append("  zet %d variabelen die WIJ NIET MEEGEVEN (waarden: --plan --json)" % len(ontbreekt))
            if alleen:
                uit.append("    alleen gezet, niet zelf gebruikt: %s" % ", ".join(alleen))
            uit.append("    ook zelf gebruikt: %s" % ", ".join(zelf))
        for e in v["env_differs"]:
            uit.append("  zet %s=%s%s -- wij geven %s mee" % (e["name"], e["value"], vw(e), e["ours"]))
        for e in v["env_same"]:
            uit.append("  zet %s=%s%s -- wij ook" % (e["name"], e["value"], vw(e)))
        for r in v["other_runs"]:
            uit.append("  start %s%s %s %s%s -- %s" % ("eerst " if r["before_game"] else "daarna ", "" if r["wait"] else "(zonder te wachten)",
                                                      r["exe"], r["args"], vw(r), "wij ook (als eigen start)" if r["ours"] else "WIJ NIET"))
            if r.get("args_missing"):
                uit.append("    argumenten die WIJ die start NIET meegeven: %s" % "  ".join("[%s]" % a for a in r["args_missing"]))
        if v["game"]:
            g = v["game"]
            uit.append("  start het spel: %s %s%s" % (g["exe"], g["args"] or "(zonder argumenten)", vw(g)))
            if v["args_missing"]:
                uit.append("    argumenten die WIJ NIET meegeven: %s" % "  ".join("[%s]" % a for a in v["args_missing"]))
            if v["args_extra"]:
                uit.append("    argumenten die wij meegeven en het script niet: %s" % "  ".join("[%s]" % a for a in v["args_extra"]))
            if not v["args_missing"] and not v["args_extra"]:
                uit.append("    de argumenten zijn gelijk aan de onze")
            if v["cwd_same"] is False:
                uit.append("    werkmap volgens het script: %s -- de onze: %s" % (v["cwd_script"], v["cwd_ours"]))
            elif v["cwd_script"] is None:
                uit.append("    werkmap: het script kiest er geen (de map van waaruit de kast het start); de onze: %s" % v["cwd_ours"])
        else:
            uit.append("  het script start ons spel niet onder dezelfde naam (zie de regels hierboven)")
    if feiten["drives"]:
        uit.append("  gebruikt de schijfletters %s" % ", ".join(l + ":" for l in feiten["drives"]))
    for c in feiten["cabinet"]:
        uit.append("  kastding (daarom niet uitvoeren): %s%s" % (c["command"], vw(c)))
    for u in feiten["unfollowed"]:
        uit.append("  niet gevolgd: %s" % u)
    return uit


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description="Lees het eigen startscript van een spel (voert niets uit)")
    ap.add_argument("script")
    ap.add_argument("--root", help="de map van het spel (standaard: de map van het script)")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    if not os.path.isfile(a.script):
        print("startscript: %s bestaat niet" % a.script, file=sys.stderr)
        return 1
    f = lees(a.script, a.root)
    if a.json:
        print(json.dumps(f, indent=1, ensure_ascii=False))
    else:
        print("\n".join(regels(f)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
