#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""De camera voor een spel (docs/per-game-patches.md, "De camera").

Een spel krijgt alleen een camera als het hem IN het spel gebruikt. Dat staat per game in game.ini (de gebruikerslaag gaat per
sleutel voor de meegeleverde):

    [Camera]
    Use = game      ; game     = het spel gebruikt de camera tijdens het spelen: het krijgt het beeld van de echte camera
                    ; presence = het spel wil alleen dat er een camera IS (anders blijft er een melding staan of wacht het):
                    ;            het krijgt een VIRTUELE camera met een kunstbeeld, nooit de echte
                    ; setup    = alleen voor de inrichting van de kast: NIET doorgegeven, het spel draait zonder
                    ; none     = het spel heeft niets met een camera (de standaard, ook zonder deze sectie)
    Device = auto   ; auto  = de camera die in de instellingen van de app gekozen is
                    ; /dev/videoN = precies die; none = geen

WELKE camera "auto" is, kiest de gebruiker in de app (Settings, "Camera for games"); dat staat in ravengun-app.conf als
[Games] camera = auto | none | /dev/videoN. Zonder keuze is het de eerste camera van de machine.

Gebruik:
    camera.py list                 de camera's van deze machine: apparaat<TAB>naam
    camera.py resolve GAME         een regel: apparaat<TAB>rol<TAB>uitleg
                                   apparaat: /dev/videoN (de echte camera), "virtual" (een virtuele met een kunstbeeld),
                                   of "-" (er wordt geen camera doorgegeven)
Exitcode 2 bij een instelling die niet klopt (een onbekende rol, of een apparaat dat geen /dev/videoN is).
"""
import os
import re
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python3 -I neemt de scriptmap niet mee
import gamesettings as gs  # noqa: E402

ROLLEN = ("game", "presence", "setup", "none")
VIRTUEEL = "virtual"                            # geen echt apparaat: de virtuele camera met een kunstbeeld
APPARAAT = re.compile(r"^/dev/video[0-9]+$")    # niets anders mag een game.ini de sandbox in vragen


class Fout(Exception):
    pass


def lijst(sys_root="/sys/class/video4linux"):
    """De camera's van deze machine: [(apparaat, naam)], op nummer. Een camera heeft vaak twee nodes (beeld en metagegevens);
    alleen de eerste van elke camera (index 0) telt."""
    uit = []
    try:
        namen = sorted(os.listdir(sys_root), key=lambda n: int(n[5:]) if n[5:].isdigit() else 1 << 30)
    except OSError:
        return uit
    for n in namen:
        if not re.match(r"^video[0-9]+$", n):
            continue
        def lees(veld, _n=n):
            try:
                with open(os.path.join(sys_root, _n, veld), encoding="utf-8", errors="replace") as f:
                    return f.read().strip()
            except OSError:
                return ""
        if lees("index") not in ("", "0"):
            continue
        uit.append(("/dev/" + n, lees("name") or n))
    return uit


def app_keuze(conf=None):
    """De keuze uit de instellingen van de app: 'auto', 'none' of /dev/videoN. Zonder bestand of sleutel: 'auto'."""
    if conf is None:
        x = os.environ.get("XDG_CONFIG_HOME", "")
        basis = x if x.startswith("/") else os.path.join(os.environ.get("HOME", os.path.expanduser("~")), ".config")
        conf = os.path.join(basis, "ravengun", "ravengun-app.conf")
    try:
        with open(conf, encoding="utf-8", errors="replace") as f:
            regels = f.read().split("\n")
    except OSError:
        return "auto"
    sectie = ""
    for r in regels:
        r = r.strip()
        if r.startswith("[") and r.endswith("]"):
            sectie = r[1:-1].strip().lower()
        elif sectie == "games" and "=" in r:
            k, v = r.split("=", 1)
            if k.strip().lower() == "camera":
                return v.strip().strip('"') or "auto"
    return "auto"


def _apparaat(waarde, bron):
    w = waarde.strip()
    if w.lower() in ("", "auto"):
        return "auto"
    if w.lower() in ("none", "geen", "off"):
        return "none"
    if not APPARAAT.match(w):
        raise Fout("%s: '%s' is geen camera (auto, none of /dev/videoN)" % (bron, waarde))
    return w


def kies(rol_ruw, apparaat_ruw, keuze_ruw, cameras):
    """De beslissing: (apparaat of '', rol, uitleg)."""
    rol = (rol_ruw or "none").strip().lower()
    if rol not in ROLLEN:
        raise Fout("[Camera] Use = '%s' is geen rol (game, presence, setup of none)" % rol_ruw)
    if rol == "none":
        return "", rol, "dit spel vraagt geen camera"
    if rol == "setup":
        return "", rol, "het spel wil de camera alleen voor de inrichting: niet doorgegeven"
    if rol == "presence":
        # Nooit de echte camera, wat er ook gekozen is: het spel kijkt alleen of er een is.
        return VIRTUEEL, rol, "het spel wil alleen dat er een camera is: een virtuele met een kunstbeeld, nooit de echte"
    eigen = _apparaat(apparaat_ruw or "auto", "[Camera] Device")
    if eigen == "none":
        return "", rol, "[Camera] Device = none"
    if eigen != "auto":
        return eigen, rol, "het spel gebruikt de camera; gekozen in game.ini"
    keuze = _apparaat(keuze_ruw or "auto", "de instellingen van de app ([Games] camera)")
    if keuze == "none":
        return "", rol, "het spel gebruikt de camera, maar in de instellingen staat 'geen camera'"
    if keuze != "auto":
        return keuze, rol, "het spel gebruikt de camera; gekozen in de instellingen"
    if cameras:
        return cameras[0][0], rol, "het spel gebruikt de camera; de eerste camera van de machine (%s)" % cameras[0][1]
    return "/dev/video0", rol, "het spel gebruikt de camera; er is er nu geen gevonden (/dev/video0 als hij er komt)"


def resolve(game, shipped=None, user=None, conf=None, sys_root="/sys/class/video4linux"):
    def laag(pad):
        secties, _ = gs.read_ini(gs.game_ini_path(game, pad) or os.devnull)
        return {k.lower(): v for k, v in secties.get("Camera", {}).items()}
    inst = {}
    for pad in gs.patches_roots(shipped, user):    # de zoekvolgorde van gamesettings; een latere laag gaat per sleutel voor
        inst.update(laag(pad))
    return kies(inst.get("use", ""), inst.get("device", ""), app_keuze(conf), lijst(sys_root))


def main(argv):
    if len(argv) == 1 and argv[0] == "list":
        for apparaat, naam in lijst():
            print("%s\t%s" % (apparaat, naam))
        return 0
    if len(argv) == 2 and argv[0] == "resolve":
        try:
            apparaat, rol, uitleg = resolve(argv[1])
        except Fout as e:
            print("camera.py: %s: %s" % (argv[1], e), file=sys.stderr)
            return 2
        print("%s\t%s\t%s" % (apparaat or "-", rol, uitleg))
        return 0
    print(__doc__.strip().split("Gebruik:")[1].strip(), file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
