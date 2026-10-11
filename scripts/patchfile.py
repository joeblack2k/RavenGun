#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""patchfile — de bestandspatch-motor van RavenGun.

Wat dit toepast
---------------
De patches uit `patches/<Game>/patches.ini` (en de gebruikerslaag erbovenop), op twee soorten doelen:

* **`Kind = file`** — zoeken en vervangen van bytes. Het doel mag de **lader**, de **kern** of de
  **game zelf** zijn. `Find` moet precies `Count` keer voorkomen, anders weigert de motor.
* **`Kind = memory`** — een adres in het spelproces. Voor de **hoofdmodule** (de exe van de game)
  rekenen we dat adres om naar een plaats in het bestand met `addrmap`, waarmee het een gewone
  bestandspatch wordt. Voor een DLL die pas later geladen wordt kan dat niet; dat wordt geweigerd
  met een duidelijke melding.

Waarom dit kan
--------------
Zie `docs/research/kern-abi.md` en `docs/community-model.md`. Kort: `Module + Offset` is voor de
hoofd-exe terug te rekenen naar een bestandsplaats via de sectie- of segmenttabel, dus is er geen
injectie en geen procesgeheugen nodig. Bewezen op Jurassic Park: het GOT-adres `0x8add63c` komt
uit op bestandsplaats `0xa9563c`, en daar staan inderdaad de vier bytes `20 01 50 b7`
(`glGetIntegerv`, `base(libGL) 0xb7500000 + 0x120`).

Wat dit niet doet
-----------------
* Nooit op de NAS schrijven. Een doel op de NAS wordt geweigerd, ook met `--force`.
* Nooit stilzwijgend iets toepassen waarvan de originele bytes niet kloppen.
* Nooit een patch toepassen die de motor niet kent; die wordt met een melding overgeslagen.

Gebruik als module
------------------
    import patchfile
    verslag = patchfile.apply_game("JurassicPark", exe, materialize=True)
    for regel in verslag.lines():
        print(regel)

Gebruik als programma
---------------------
    python3 scripts/patchfile.py --game JurassicPark --game-exe /pad/naar/game --check
    python3 scripts/patchfile.py --game JurassicPark --game-exe /pad/naar/game --apply
"""
import argparse
import hashlib
import os
import shutil
import struct
import sys

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import addrmap                      # noqa: E402
import gamesettings as gs           # noqa: E402

ROOT = os.path.dirname(HERE)
HOME = os.environ.get("HOME", "/home/deck")

# Waar de NAS gemount is. Nooit beschrijven.
NAS_ROOTS = tuple(p for p in (os.environ.get("RAVENGUN_NAS_ROOT", ""), os.path.join(HOME, "mnt")) if p)

# De bytes voor `Action = jmp-returns-true`: mov eax,1 ; ret  (x86 en x64; op x64 wist mov eax,1
# het bovenste deel van rax, dus het resultaat is 1). Past alleen op het BEGIN van een functie.
RETURNS_TRUE = b"\xb8\x01\x00\x00\x00\xc3"

# Acties die deze motor kent, met het aantal bytes dat ze minimaal nodig hebben.
ACTIONS = ("nop", "jmp-returns-true", "replace-bytes", "write-bytes")


class Refused(Exception):
    """De patch of het doel is niet toegestaan; er is niets geschreven."""


def on_nas(path):
    rp = os.path.realpath(path)
    return any(rp.startswith(os.path.realpath(r) + os.sep) or rp == os.path.realpath(r) for r in NAS_ROOTS)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_hex(raw, key, patch_name):
    try:
        return bytes.fromhex("".join(str(raw).split()))
    except ValueError:
        raise Refused("patch '%s': %s is geen hexadecimale bytereeks (%r)" % (patch_name, key, raw))


class Result:
    """Het verslag van één patch."""

    def __init__(self, name, kind, target, place, status, reason="", before=None, after=None):
        self.name = name
        self.kind = kind
        self.target = target
        self.place = place
        self.status = status          # toegepast | overgeslagen | geweigerd | gecontroleerd
        self.reason = reason
        self.before = before
        self.after = after

    def line(self):
        s = "%-11s %-38s %-22s %s" % (self.status, self.name[:38], self.place[:22], self.reason)
        if self.before is not None and self.after is not None and self.before != self.after:
            s += "  [%s -> %s]" % (self.before.hex(), self.after.hex())
        return s.rstrip()


class Report:
    def __init__(self, game):
        self.game = game
        self.results = []
        self.written = {}             # doel -> het bestand dat nu de gepatchte versie is

    def add(self, r):
        self.results.append(r)
        return r

    def count(self, status):
        return sum(1 for r in self.results if r.status == status)

    def lines(self):
        out = ["patches van %s:" % self.game]
        for r in self.results:
            out.append("  " + r.line())
        out.append("  toegepast: %d   overgeslagen: %d   geweigerd: %d" % (
            self.count("toegepast") + self.count("gecontroleerd"), self.count("overgeslagen"), self.count("geweigerd")))
        return out

    def summary(self):
        return "toegepast %d, overgeslagen %d, geweigerd %d" % (
            self.count("toegepast") + self.count("gecontroleerd"), self.count("overgeslagen"), self.count("geweigerd"))


# ---------------------------------------------------------------------------------------------
# Doelen

def game_targets(exe_path, loader=None, core=None):
    """De bestanden die gepatcht mogen worden, met hun rolnaam."""
    t = {}
    if loader:
        t["loader"] = loader
    if core:
        t["core"] = core
    if exe_path:
        t["game"] = exe_path
    return t


def pick_target(patch, targets, exe_path):
    """Welk bestand bedoelt deze patch? Geeft (rolnaam, pad).

    `Target` is `loader`, `core` of `game`. Staat er niets, dan kijkt de motor naar `Module`: is dat
    de hoofdmodule (de exe van de game), dan is het doel `game`."""
    t = (patch.get("Target") or "").strip().lower()
    if t:
        if t not in targets:
            raise Refused("patch '%s': doel '%s' is er niet voor deze game (wel: %s)"
                          % (patch.name, t, ", ".join(sorted(targets)) or "geen"))
        return t, targets[t]
    module = (patch.get("Module") or "").strip()
    if module and exe_path and os.path.basename(module).lower() == os.path.basename(exe_path).lower():
        return "game", targets["game"]
    if module:
        raise Refused(
            "patch '%s': Module = %s is niet de hoofdmodule van deze game (%s). Een patch in een DLL die pas "
            "later geladen wordt kan niet in het bestand; die vraagt de geheugen-motor (docs/community-model.md, punt 3)"
            % (patch.name, module, os.path.basename(exe_path or "?")))
    raise Refused("patch '%s': geen Target en geen Module; de motor weet niet waar hij moet schrijven" % patch.name)


def pick_kind(patch):
    k = patch.kind
    if k in ("file", "memory"):
        return k
    raise Refused("patch '%s': soort '%s' wordt niet door deze motor toegepast" % (patch.name, k or "(geen)"))


# ---------------------------------------------------------------------------------------------
# De bytes van een memory-patch

def memory_bytes(patch, arch_bits):
    """De bytes die een `Kind = memory`-patch op zijn plaats zet, plus de grootte."""
    action = (patch.get("Action") or "").strip().lower()
    if not action:
        raise Refused("patch '%s': Action ontbreekt (toegestaan: %s)" % (patch.name, ", ".join(ACTIONS)))
    if action not in ACTIONS:
        raise Refused("patch '%s': Action '%s' kent de motor niet (toegestaan: %s)"
                      % (patch.name, action, ", ".join(ACTIONS)))
    size = patch.get("Size", "").strip()
    try:
        size = int(size, 0) if size else 0
    except ValueError:
        raise Refused("patch '%s': Size is geen getal (%r)" % (patch.name, size))
    if action == "nop":
        if not size:
            raise Refused("patch '%s': Action = nop vraagt Size" % patch.name)
        return b"\x90" * size, size
    if action == "jmp-returns-true":
        if not size:
            raise Refused("patch '%s': Action = jmp-returns-true vraagt Size" % patch.name)
        if size < len(RETURNS_TRUE):
            raise Refused("patch '%s': Action = jmp-returns-true heeft minstens %d bytes nodig, Size is %d"
                          % (patch.name, len(RETURNS_TRUE), size))
        return RETURNS_TRUE + b"\x90" * (size - len(RETURNS_TRUE)), size
    # replace-bytes / write-bytes: de bytes staan in Replace
    raw = patch.get("Replace", "")
    if not raw.strip():
        raise Refused("patch '%s': Action = %s vraagt Replace met de nieuwe bytes" % (patch.name, action))
    data = parse_hex(raw, "Replace", patch.name)
    if size and len(data) != size:
        raise Refused("patch '%s': Replace is %d bytes maar Size zegt %d" % (patch.name, len(data), size))
    return data, len(data)


# ---------------------------------------------------------------------------------------------
# Toepassen

def _patch_file_bytes(data, patch):
    """Zoeken en vervangen in een buffer. Geeft de nieuwe buffer."""
    find = parse_hex(patch.get("Find", ""), "Find", patch.name)
    repl = parse_hex(patch.get("Replace", ""), "Replace", patch.name)
    if not find:
        raise Refused("patch '%s': Kind = file vraagt Find" % patch.name)
    if len(find) != len(repl):
        raise Refused("patch '%s': Find is %d bytes en Replace %d; die moeten even lang zijn"
                      % (patch.name, len(find), len(repl)))
    try:
        want = int(patch.get("Count", "1") or "1", 0)
    except ValueError:
        raise Refused("patch '%s': Count is geen getal (%r)" % (patch.name, patch.get("Count")))
    found = data.count(find)
    if found != want:
        raise Refused("patch '%s': Find komt %d keer voor, Count zegt %d (verkeerde spelversie?)"
                      % (patch.name, found, want))
    return data.replace(find, repl), len(repl) * found


def apply_game(game_name, exe_path=None, loader=None, core=None, materialize=False,
               patches_dir=None, exe_sha256=None, allow_in_place=True, take_over_core=False):
    """Past de patches van één game toe, met de instellingen van die game opnieuw gelezen.

    Let op: dit leest `patches.ini` opnieuw. Draait er al iets dat de patches onderweg aanpast
    (zoals de launcher, die de lader-patch uitzet bij de eigen lader), gebruik dan
    `apply_game_obj()` met die al geladen Game -- anders zie je een andere uitkomst dan de start."""
    kwargs = {}
    if patches_dir:
        kwargs["shipped"] = patches_dir
    return apply_game_obj(gs.load_game(game_name, **kwargs), game_name, exe_path, loader, core,
                          materialize, exe_sha256, allow_in_place, take_over_core)


def apply_game_obj(g, game_name, exe_path=None, loader=None, core=None, materialize=False,
                   exe_sha256=None, allow_in_place=True, take_over_core=False):
    """Als `apply_game()`, maar met een **al geladen** Game. Zie de docstring daar.

    materialize = False: alleen controleren, niets schrijven.
    materialize = True:  de lader en de kern worden als kopie weggeschreven, de exe van de game
                         wordt in het bestand zelf aangepast (dat moet lokaal zijn, nooit op de NAS).

    take_over_core = True: ook de patches toepassen die `AppliedBy = core` hebben. Die staan in de
    ini als *beschrijving van wat de kern zelf doet*; zolang wij de kern niet vervangen mag de motor
    ze niet nog eens toepassen. Zodra onze eigen kern ze overneemt, zet je deze vlag aan."""
    report = Report(game_name)
    targets = game_targets(exe_path, loader, core)

    for err in g.patch_errors:
        if err["enabled"]:
            report.add(Result(err["name"], "?", "-", "-", "geweigerd", err["message"]))
        else:
            report.add(Result(err["name"], "?", "-", "-", "overgeslagen", "afgewezen: " + err["message"]))

    # Versiecontrole op de exe, net als de launcher doet.
    want_sha = (exe_sha256 or g.get("Game", "ExeSha256", "") or "").strip().lower()
    if want_sha and exe_path and os.path.isfile(exe_path):
        got = sha256_file(exe_path)
        if got != want_sha:
            raise Refused("de patches van %s gelden voor een exe met sha256 %s, maar %s heeft %s"
                          % (game_name, want_sha[:16], exe_path, got[:16]))

    buffers = {}
    for p in g.patches:
        if not p.enabled:
            report.add(Result(p.name, p.kind or "?", p.get("Target") or p.get("Module") or "-", "-",
                              "overgeslagen", "uit"))
            continue
        if p.applied_by == "core" and not take_over_core:
            report.add(Result(p.name, p.kind or "?", p.get("Target") or p.get("Module") or "-", "-",
                              "overgeslagen",
                              "AppliedBy = core: de kern past dit zelf toe (--take-over-core om het over te nemen)"))
            continue
        try:
            kind = pick_kind(p)
            role, path = pick_target(p, targets, exe_path)
            # Controleren van een doel op de NAS mag (we lezen alleen); schrijven nooit.
            if materialize and on_nas(path):
                raise Refused("het doel %s ligt op de NAS; daar wordt nooit een bestand gepatcht" % path)
            if not os.path.isfile(path):
                raise Refused("het doel %s bestaat niet" % path)
            if role not in buffers:
                with open(path, "rb") as fh:
                    buffers[role] = bytearray(fh.read())

            if kind == "file":
                new, changed = _patch_file_bytes(bytes(buffers[role]), p)
                buffers[role] = bytearray(new)
                report.add(Result(p.name, kind, role, "%s+%s" % (role, p.get("Find", "")[:11]),
                                  "toegepast" if materialize else "gecontroleerd",
                                  "%d byte(s) op %d plek(ken)" % (changed, int(p.get("Count", "1") or 1))))
                continue

            # Kind = memory, hoofdmodule: het adres naar een bestandsplaats rekenen.
            m = addrmap.open_module(path)
            offset = int(p.get("Offset", "0"), 0)
            data, size = memory_bytes(p, 8)
            file_off = m.offset_for(offset)
            region = m.region_of(offset)
            before = bytes(buffers[role][file_off:file_off + size])
            if len(before) < size:
                raise Refused("patch '%s': %d bytes op %s+0x%x lopen voorbij het einde van het bestand"
                              % (p.name, size, os.path.basename(path), offset))
            find = parse_hex(p.get("Find", ""), "Find", p.name)
            if find and before != find:
                raise Refused("patch '%s': de bytes op %s+0x%x zijn %s, Find zegt %s (verkeerde spelversie?)"
                              % (p.name, os.path.basename(path), offset, before.hex(), find.hex()))
            note = "op %s+0x%x (bestand 0x%x, %s)" % (os.path.basename(path), offset, file_off, region)
            if not find:
                note += "; geen Find, dus niet te controleren"
            buffers[role][file_off:file_off + size] = data
            report.add(Result(p.name, kind, role, "%s+0x%x" % (os.path.basename(path), offset),
                              "toegepast" if materialize else "gecontroleerd", note,
                              before=before, after=data))
        except (Refused, addrmap.NotAnImage, addrmap.AddressOutsideImage) as e:
            report.add(Result(p.name, p.kind or "?", p.get("Target") or p.get("Module") or "-", "-",
                              "geweigerd", str(e)))

    if materialize:
        for role, buf in buffers.items():
            path = targets[role]
            orig = bytes(open(path, "rb").read())
            if bytes(buf) == orig:
                continue                      # niets veranderd: geen kopie maken
            if role == "game":
                if not allow_in_place:
                    raise Refused("de exe van de game mag hier niet in het bestand aangepast worden")
                if on_nas(path):
                    raise Refused("de exe van de game ligt op de NAS; patch een kopie in de view")
                bak = path + ".ravengun-orig"
                if not os.path.exists(bak):
                    shutil.copy2(path, bak)
                with open(path, "wb") as fh:
                    fh.write(bytes(buf))
                report.written[role] = path
            else:
                out = os.path.join(os.path.dirname(path), os.path.basename(path) + ".ravengun-patched")
                with open(out, "wb") as fh:
                    fh.write(bytes(buf))
                report.written[role] = out
    return report


# ---------------------------------------------------------------------------------------------
# De runtime-route: de lijst voor de geheugen-motor

def runtime_list(g, exe_path=None, take_over_core=False):
    """De patchlijst voor de **geheugen-motor** (`memory/src/patchmem.c`), uit `patches.ini`.

    De bestandsroute (`apply_game_obj`) dekt alles wat in het bestand van een module staat. Er zijn
    twee gevallen waarin dat niet kan, en die horen hier:

      * `Kind = memory` waarvan `Module` **niet de hoofdmodule** is -- een DLL die pas later geladen
        wordt staat niet in het bestand van de exe;
      * een **gepakte** exe, waarin de bytes in het bestand niet de bytes zijn die het spel uitvoert.

    Het tweede kan deze functie niet zien (daarvoor moet je het bestand lezen), dus die patch komt
    gewoon in de lijst; de motor controleert zelf met `verwacht` en weigert als het niet klopt.

    Formaat (regels met `|`; `#` en lege regels worden overgeslagen):

        # module|offset|verwacht|nieuw
        game.exe|0x113E80||90909090
        UnityPlayer.dll|0x1234|48895c24|deadbeef

    Geeft `(regels, verslag)`: de tekst voor `patchmem.txt` en per patch waarom hij er wel of niet in
    staat. De module staat als **bestandsnaam** (niet als pad), en `offset` is een RVA ten opzichte
    van de basis van die module -- precies wat de motor opzoekt."""
    import os as _os
    hoofd = _os.path.basename(exe_path).lower() if exe_path else ""
    regels, verslag = [], []
    for p in g.patches:
        if not p.enabled:
            continue
        if p.applied_by == "core" and not take_over_core:
            verslag.append((p.name, "overgeslagen", "AppliedBy = core: de kern past dit zelf toe"))
            continue
        if p.kind != "memory":
            verslag.append((p.name, "overgeslagen", "soort %s hoort niet in de runtime-lijst" % (p.kind or "(geen)")))
            continue
        module = (p.get("Module") or "").strip()
        if not module:
            verslag.append((p.name, "geweigerd", "Module ontbreekt; de motor weet niet waar hij moet schrijven"))
            continue
        if hoofd and _os.path.basename(module).lower() == hoofd:
            verslag.append((p.name, "overgeslagen",
                            "%s is de hoofdmodule; die doet de bestandsroute al" % module))
            continue
        try:
            offset = int(p.get("Offset", "0") or "0", 0)
            data, _size = memory_bytes(p, 64)
        except (Refused, ValueError) as e:
            verslag.append((p.name, "geweigerd", str(e)))
            continue
        find = parse_hex(p.get("Find", ""), "Find", p.name) if p.get("Find", "").strip() else b""
        regels.append("%s|0x%x|%s|%s" % (_os.path.basename(module), offset, find.hex(), data.hex()))
        verslag.append((p.name, "in de lijst", "%s+0x%x, %d byte(s)%s"
                        % (_os.path.basename(module), offset, len(data),
                           "" if find else "; geen Find, dus niet te controleren")))
    kop = ["# Runtime-patchlijst voor de geheugen-motor van RavenGun (memory/src/patchmem.c).",
           "# Geschreven uit patches.ini; niet met de hand aanpassen.",
           "# module|offset|verwacht|nieuw"]
    return "\n".join(kop + regels) + "\n", verslag


def schrijf_runtime_list(pad, g, exe_path=None, take_over_core=False):
    """Schrijft de runtime-lijst naar `pad` en geeft `(pad, aantal regels, verslag)`."""
    tekst, verslag = runtime_list(g, exe_path=exe_path, take_over_core=take_over_core)
    aantal = sum(1 for r in tekst.splitlines() if r and not r.startswith("#"))
    if aantal:
        with open(pad, "w", encoding="utf-8") as fh:
            fh.write(tekst)
        return pad, aantal, verslag
    return None, 0, verslag


def main(argv=None):
    ap = argparse.ArgumentParser(description="Pas de patches van een game toe op een lokale kopie.")
    ap.add_argument("--game", required=True, help="de naam van de map onder patches/")
    ap.add_argument("--game-exe", default="", help="het pad van de exe van de game (lokaal, niet op de NAS)")
    ap.add_argument("--loader", default="", help="het pad van de lader")
    ap.add_argument("--core", default="", help="het pad van de kern")
    ap.add_argument("--patches-dir", default="", help="de map met de patches (standaard <repo>/patches)")
    ap.add_argument("--apply", action="store_true", help="ook echt schrijven (standaard: alleen controleren)")
    ap.add_argument("--take-over-core", action="store_true",
                    help="ook patches met AppliedBy = core toepassen (nodig zodra onze eigen kern die taak overneemt)")
    ap.add_argument("--list", action="store_true", help="alleen de patches tonen")
    args = ap.parse_args(argv)

    try:
        rep = apply_game(args.game, args.game_exe or None, args.loader or None, args.core or None,
                         materialize=args.apply, patches_dir=args.patches_dir or None,
                         take_over_core=args.take_over_core)
    except Refused as e:
        print("geweigerd: %s" % e, file=sys.stderr)
        return 5
    for line in rep.lines():
        print(line)
    for role, path in sorted(rep.written.items()):
        print("  geschreven: %s -> %s" % (role, path))
    return 1 if rep.count("geweigerd") else 0


if __name__ == "__main__":
    raise SystemExit(main())
