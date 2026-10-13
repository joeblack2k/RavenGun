#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""nas-schrijfpaden-scan -- welke plekken van een game-view komen (via een symlink) op de NAS uit, en van wie zijn die?

Ontwerp (besluit van de eigenaar, 2026-10-12): de games WONEN op de NAS en worden daarvandaan gestart. Wat een game of de kern
zelf in zijn eigen spelmap schrijft (Ram/, Log/, eigen *.ini/*.json/*.conf, NVRAM, <exe>.dxvk-cache naast de exe, en het
teknoparrot.ini dat de kern in de werkmap schrijft) is NORMAAL. Wat een regel blijft: onze EIGEN artefacten komen nooit
stilzwijgend in een spelmap op de NAS: drop-ins, patchbestanden, onze plugin-DLL's met hun config/cache/log (BepInEx/), hulpbestanden.

Daarom twee klassen:
  (a) ontwerp   de game schrijft in zijn eigen spelmap op de NAS. Informatief, GEEN bevinding. "bekend en geaccepteerd".
                Rollen: werkmap-view, werkmap-plan, werkmap-game, exe-map, schrijfmap, schrijfbestand, bepinex-van-de-game
                (de eigen BepInEx van een game uit de TeknoParrot-dump, zonder plugin van ons).
  (b) artefact  onze artefacten die op de NAS kunnen landen. Dit zijn de echte bevindingen. Rollen:
                bepinex          onze BepInEx-boom (aagames-view, of met een plugin/config van ons erin) komt op de NAS uit
                dropin-doel      de doelmap van een drop-in (scripts/launch-direct.py: dropin_plan; per-game dropin/dropin.ini) komt op de NAS uit
                plugins          Game_Data/Plugins van een aagames-view (daar komen de drop-ins) komt op de NAS uit
                gepatcht         de map voor gepatchte binaries (~/.local/share/ravengun/patched) komt op de NAS uit
                aanwezig-op-nas  een artefact van ons staat al in een spelmap op de NAS (restant; opruimen is aan de eigenaar)
                Status per bevinding: "open" = zonder bescherming (telt mee in uitkomst/afsluitcode), "gedekt" = launch-direct
                maakt de map bij het plaatsen lokaal (dropin_materialiseer) en place_binary weigert de NAS, "restant" = al aanwezig.

Dit script leest alleen (os.lstat/readlink/listdir, sha256 van drop-inbestanden, en `launch-direct.py --plan --json`, dat niets
start) en schrijft niets, ook niet op de NAS.

Gebruik (in ~/projects/ravengun):
    python3 -I tools/nas-schrijfpaden-scan.py              # leesbaar overzicht van alle views
    python3 -I tools/nas-schrijfpaden-scan.py --md         # twee tabellen in markdown (voor docs/research/nas-schrijfpaden-inventaris.md)
    python3 -I tools/nas-schrijfpaden-scan.py --json       # alles als JSON
    python3 -I tools/nas-schrijfpaden-scan.py LuigisMansion RabbidsHollywood   # alleen deze views
    python3 -I tools/nas-schrijfpaden-scan.py --geen-plan  # sla launch-direct --plan over (sneller; exe-map wordt geschat)
Afsluitcode 1 alleen als klasse (b) een bevinding met status "open" heeft, anders 0.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time

HOME = os.path.expanduser("~")
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NAS = os.path.realpath(os.path.join(HOME, "mnt", "spidernas_emulation"))
VIEWS = os.path.join(HOME, ".local/share/ravengun/runtime/views")
VIEWS_AAGAMES = os.path.join(HOME, ".local/share/ravengun/runtime/views-aagames")
CONFIG_EXT = (".json", ".conf", ".cfg", ".ini")
EPOCH = time.mktime((2026, 10, 9, 0, 0, 0, 0, 0, -1))   # de eerste run van onze eigen code (de repo begon op 2026-10-09)

# mapnamen (kleine letters) waar een game of onze kern in schrijft
SCHRIJFNAMEN = {
    "log": "logbestanden van de game of loader", "logs": "logbestanden",
    "ram": "NVRAM/RAM-bestanden van de game", "nvram": "NVRAM van de game", "sram": "SRAM van de game",
    "eeprom": "EEPROM van de game", "save": "savegames", "saves": "savegames", "savedata": "savegames",
    "config": "configuratie die de game bijwerkt", "configs": "configuratie", "cache": "cachebestanden",
    "teknoparrot": "kern: schijfletter-herleiding naar .\\TeknoParrot\\<letter>\\ (patch 0004), opslag",
    "user": "gebruikersgegevens", "userdata": "gebruikersgegevens", "data_user": "gebruikersgegevens",
    "coinaud": "muntgegevens", "messages": "berichten",
}
SCHRIJVERS = {
    "werkmap-view": "play-unity.sh/Proton: werkmap van de game; de game (logs Gun_*.log, NVRAM)",
    "werkmap-plan": "launch-direct: werkmap van het lader-proces; relatieve schrijfacties van de loader",
    "werkmap-game": "OpenParrot-kern (teknoparrot.ini [Error], ScoreSubmission.ini, .\\TeknoParrot\\<letter>\\), "
                    "game (logs/NVRAM relatief aan de werkmap)",
    "exe-map": "teknoparrot.ini door de kern (launch-direct weigert op de NAS), DXVK state cache (<exe>.dxvk-cache), game",
}
BESLUIT = "bekend en geaccepteerd (besluit eigenaar 2026-10-12: de games wonen op de NAS)"
# namen van onze eigen artefacten (voor het zoeken van restanten op de NAS)
ONZE_NAMEN = re.compile(r"ravengun|nl\.studiodamon|\.ravengun-tmp$|^rg-", re.I)
HEAVY = {"gi", "managed", "mono", "resources", "streamingassets", "data", "dataglobal"}   # niet in afdalen bij het zoeken naar restanten
PATCHED_ROOT = os.path.join(HOME, ".local/share/ravengun/patched")


def onder(pad, wortel=None):
    wortel = wortel or NAS
    p, w = os.path.realpath(pad), os.path.realpath(wortel)
    return p == w or p.startswith(w + os.sep)


def eerste_link_naar_nas(view, pad, nas):
    """De eerste component van pad (vanaf de view) die een symlink is die onder de NAS uitkomt, of None."""
    rel = os.path.relpath(pad, view)
    cur = view
    for deel in [d for d in rel.split(os.sep) if d not in ("", ".")]:
        cur = os.path.join(cur, deel)
        if os.path.islink(cur) and onder(cur, nas):
            return cur
    return None


def tp_pad_naar_unix(p):
    """Z:\\home\\deck\\x -> /home/deck/x (Wine-pad uit TP_GAME_WORKING_DIRECTORY)."""
    if not p:
        return None
    m = re.match(r"^[zZ]:[\\/](.*)$", p)
    return "/" + m.group(1).replace("\\", "/") if m else None


def zoek_exe_mappen(view):
    """Terugval als er geen startplan is: mappen (diepte <= 2, ook via symlinks) met een .exe."""
    uit = set()
    for e in os.listdir(view):
        p = os.path.join(view, e)
        if e.lower().endswith(".exe"):
            uit.add(view)
        elif os.path.isdir(p):
            try:
                for f in os.listdir(p):
                    if f.lower().endswith(".exe"):
                        uit.add(p)
            except OSError:
                pass
    return sorted(uit)


def exe_uit_gameini(naam, view, maxdiepte=7):
    """Terugval voor profielen zonder startplan (bijv. twee exe's): de exe-namen uit patches/<naam>/game.ini
    (Executable, Executable2) zoeken in de view, ook door symlinks heen. Geeft lijst mappen."""
    ini = os.path.join(REPO, "patches", naam, "game.ini")
    namen = set()
    try:
        with open(ini, encoding="utf-8-sig") as f:
            for regel in f:
                m = re.match(r"^\s*Executable2?\s*=\s*(\S.*?)\s*$", regel)
                if m:
                    namen.add(os.path.basename(m.group(1).replace("\\", "/")).lower())
    except OSError:
        return []
    uit = set()
    basis = len(view.rstrip(os.sep).split(os.sep))
    for d, dirs, files in os.walk(view, followlinks=True):
        if len(d.split(os.sep)) - basis >= maxdiepte:
            dirs[:] = []
        if any(f.lower() in namen for f in files):
            uit.add(d)
    return sorted(uit)


def bewijs(map_pad, exe=None, diepte=1, max_n=10):
    """Bestanden in map_pad (op de NAS) waarvan de mtime laat zien dat onze runs ze schreven: nieuwer dan 2026-10-09 en
    nieuwer dan de exe (+1 uur). Alleen lezen. Geeft lijst (relatief pad, datum, grootte). Heuristiek, geen bewijs op zich."""
    drempel = EPOCH
    if exe and os.path.exists(exe):
        drempel = max(drempel, os.stat(exe).st_mtime + 3600)
    uit = []
    basis = os.path.realpath(map_pad)

    def loop(d, n):
        try:
            namen = sorted(os.listdir(d))
        except OSError:
            return
        for e in namen:
            p = os.path.join(d, e)
            try:
                st = os.lstat(p)
            except OSError:
                continue
            if os.path.isdir(p) and not os.path.islink(p):
                if n < diepte:
                    loop(p, n + 1)
            elif st.st_mtime >= drempel:
                uit.append((os.path.relpath(p, basis), time.strftime("%Y-%m-%d %H:%M", time.localtime(st.st_mtime)), st.st_size))

    loop(basis, 1)
    return sorted(uit, key=lambda x: x[1])[-max_n:]


def ontwerp_view(view, nas=None, spel=None, cwd=None, workdir=None, aagames=False, exe_mappen=None):
    """KLASSE (a), ontwerp: plekken waar de game zelf in zijn eigen spelmap op de NAS schrijft. Informatief, geen bevinding. Elk item:
    {map, wijst_naar, via, rollen, schrijvers, bewijs}. Leest alleen.

    spel: pad van de exe in de view (dan is de exe-map zijn map); cwd: werkmap uit het startplan; workdir: het unix-pad van
    TP_GAME_WORKING_DIRECTORY; aagames: de view is een aagames-view (werkmap = de view)."""
    nas = os.path.realpath(nas or NAS)
    kand = {}   # pad -> set(rollen)

    def voeg(pad, rol):
        if pad and os.path.isdir(pad):
            kand.setdefault(os.path.normpath(pad), set()).add(rol)

    if aagames:
        voeg(view, "werkmap-view")
    exe = spel
    if spel:
        voeg(os.path.dirname(spel), "exe-map")
    for m in exe_mappen or ():
        voeg(m, "exe-map")
    voeg(cwd, "werkmap-plan")
    voeg(workdir, "werkmap-game")
    # mapnamen die op schrijven wijzen: in de view en in de exe-map (diepte 1), ook als de map zelf een symlink is
    for basis in {view, os.path.dirname(spel) if spel else view}:
        try:
            namen = os.listdir(basis)
        except OSError:
            continue
        for e in namen:
            p = os.path.join(basis, e)
            if e.lower() in SCHRIJFNAMEN and os.path.isdir(p):
                voeg(p, "schrijfmap")
    bep = os.path.join(view, "BepInEx")
    if os.path.isdir(bep) and not aagames and not bepinex_is_van_ons(bep):
        # de eigen BepInEx van de game (TeknoParrot-dump): BepInEx schrijft config/cache/log naast de game, dat is ontwerp
        for sub in ("", "plugins", "config", "cache", "patchers"):
            sp = os.path.join(bep, sub) if sub else bep
            if os.path.lexists(sp) and onder(sp, nas) and os.path.isdir(sp):
                voeg(sp, "bepinex-van-de-game")
    uit = []
    for pad, rollen in sorted(kand.items()):
        if not onder(pad, nas):
            continue
        schrijvers = []
        for r in ("werkmap-view", "werkmap-plan", "werkmap-game", "exe-map"):
            if r in rollen and SCHRIJVERS[r] not in schrijvers:
                schrijvers.append(SCHRIJVERS[r])
        if "bepinex-van-de-game" in rollen:
            schrijvers.append("de eigen BepInEx van de game (TeknoParrot-dump, zonder plugin van ons): config, cache, LogOutput.log")
        if "schrijfmap" in rollen:
            schrijvers.append(SCHRIJFNAMEN.get(os.path.basename(pad).lower(), "game"))
        via = eerste_link_naar_nas(view, pad, nas) if pad != view else None
        diepte = 1
        uit.append({"map": os.path.relpath(pad, view) if pad != view else ".", "pad": pad,
                    "wijst_naar": os.path.realpath(pad), "via": os.path.relpath(via, view) if via else ".",
                    "rollen": sorted(rollen), "klasse": "ontwerp", "status": BESLUIT, "soort": "map", "bewezen": None, "schrijvers": schrijvers,
                    "bewijs": bewijs(pad, exe, diepte)})
    # losse bestanden: in een lokale werkmap/exe-map kan een bestand een symlink naar de NAS zijn waar de game in schrijft
    # (bijv. ControllerBaseConfig.json); een schrijfactie volgt de link en komt op de NAS uit.
    drempel = EPOCH
    if exe and os.path.exists(exe):
        drempel = max(drempel, os.stat(exe).st_mtime + 3600)
    for pad in sorted(set(kand) | {view}):
        if onder(pad, nas):
            continue
        try:
            namen = sorted(os.listdir(pad))
        except OSError:
            continue
        for e in namen:
            f = os.path.join(pad, e)
            if not (os.path.islink(f) and not os.path.isdir(f) and onder(f, nas)):
                continue
            try:
                st = os.stat(f)
            except OSError:
                continue
            el = e.lower()
            if not (el in ("teknoparrot.ini", "scoresubmission.ini", "card.ini") or el.endswith(".dxvk-cache")
                    or (el.endswith(CONFIG_EXT) and st.st_size < 64 * 1024)):
                continue
            bew = [(e, time.strftime("%Y-%m-%d %H:%M", time.localtime(st.st_mtime)), st.st_size)] if st.st_mtime >= drempel else []
            uit.append({"map": os.path.relpath(f, view), "pad": f, "wijst_naar": os.path.realpath(f), "via": os.path.relpath(f, view),
                        "rollen": ["schrijfbestand"], "klasse": "ontwerp", "status": BESLUIT, "soort": "bestand", "bewezen": bool(bew),
                        "schrijvers": ["de game/kern schrijft in dit eigen bestand van de game, op de NAS"],
                        "bewijs": bew})
    return uit


def sha256(pad):
    import hashlib
    h = hashlib.sha256()
    with open(pad, "rb") as f:
        for blok in iter(lambda: f.read(1 << 20), b""):
            h.update(blok)
    return h.hexdigest()


def _datum(st):
    return time.strftime("%Y-%m-%d %H:%M", time.localtime(st.st_mtime))


def bepinex_is_van_ons(bep, aagames=False, max_diepte=3):
    """Is de BepInEx-boom van ons? Ja bij een aagames-view (wij zetten hem neer) of als er een artefact van ons in staat
    (RavenGun.*.dll, nl.studiodamon.ravengun.*.cfg), ook via symlinks. Anders is het de eigen BepInEx van de game
    (TeknoParrot-dump, bijv. met BBGTPPlugin.dll): dan is het ontwerp (klasse a)."""
    if aagames:
        return True

    def loop(d, n):
        try:
            namen = os.listdir(d)
        except OSError:
            return False
        for e in namen:
            if ONZE_NAMEN.search(e):
                return True
            p = os.path.join(d, e)
            if os.path.isdir(p) and n < max_diepte and e.lower() != "core" and loop(p, n + 1):
                return True
        return False

    return os.path.isdir(bep) and loop(os.path.realpath(bep), 1)


def artefacten_view(view, nas=None, aagames=False, dropin_doelen=(), gedekt=False):
    """KLASSE (b), onze artefacten: plekken in de view waar ONZE bestanden terechtkomen en die op de NAS uitkomen.
    - BepInEx/ (onze plugin met config, cache, LogOutput.log): de map zelf, plugins/config/cache/patchers en LogOutput.log;
    - elke doelmap van een drop-in (dropin_doelen: de doelpaden van de bestanden; de eerste bestaande map erboven telt);
    - aagames: Game_Data/Plugins (daar komen de drop-ins).
    gedekt: launch-direct maakt een symlinkmap bij het plaatsen lokaal (dropin_materialiseer) en place_binary weigert de NAS;
    de bevinding krijgt dan status "gedekt" in plaats van "open". Leest alleen."""
    nas = os.path.realpath(nas or NAS)
    kand = {}   # pad -> (rol, status)

    def voeg(pad, rol, status):
        pad = os.path.normpath(pad)
        if pad not in kand:
            kand[pad] = ([], status)
        if rol not in kand[pad][0]:
            kand[pad][0].append(rol)
        if status == "open":
            kand[pad] = (kand[pad][0], "open")

    bep = os.path.join(view, "BepInEx")
    if os.path.lexists(bep) and bepinex_is_van_ons(bep, aagames):
        if onder(bep, nas) and (os.path.islink(bep) or not os.path.isdir(bep)):
            voeg(bep, "bepinex", "open")
        elif os.path.isdir(bep):
            for sub in ("plugins", "config", "cache", "patchers"):
                sp = os.path.join(bep, sub)
                if os.path.lexists(sp) and onder(sp, nas):
                    voeg(sp, "bepinex", "open")
                elif os.path.isdir(sp):
                    for d, dirs, files in os.walk(sp):
                        for x in dirs + files:
                            xp = os.path.join(d, x)
                            if os.path.islink(xp) and onder(xp, nas):
                                voeg(xp, "bepinex", "open")
            lg = os.path.join(bep, "LogOutput.log")
            if os.path.lexists(lg) and onder(lg, nas):
                voeg(lg, "bepinex", "open")
    if aagames:
        gd = os.path.join(view, "Game_Data")
        pl = os.path.join(gd, "Plugins")
        if os.path.lexists(gd) and onder(gd, nas) and os.path.isdir(gd):
            voeg(gd, "plugins", "open")
        elif os.path.lexists(pl) and onder(pl, nas):
            voeg(pl, "plugins", "open")
    for dest in dropin_doelen:
        d = os.path.dirname(os.path.normpath(dest))
        while not os.path.lexists(d) and len(d) > len(view):
            d = os.path.dirname(d)
        if onder(d, nas):
            voeg(d, "dropin-doel", "gedekt" if gedekt else "open")
    uit = []
    for pad, (rollen, status) in sorted(kand.items()):
        via = eerste_link_naar_nas(view, pad, nas) if pad != view else None
        schrijvers = []
        if "bepinex" in rollen:
            schrijvers.append("BepInEx met onze plugin: config, cache en LogOutput.log")
        if "plugins" in rollen or "dropin-doel" in rollen:
            schrijvers.append("drop-ins (matrix64.dll, matrix/matrix.ini, ravengun-hasp/, hasp-dll's) uit patches/<Game>/dropin/")
        if status == "gedekt":
            schrijvers.append("bescherming: dropin_materialiseer maakt de map lokaal, place_binary weigert de NAS")
        bew = []
        try:
            basis = os.path.realpath(pad)
            namen = os.listdir(basis) if os.path.isdir(basis) else []
            for e in sorted(namen):
                if ONZE_NAMEN.search(e):
                    bew.append((e, _datum(os.lstat(os.path.join(basis, e))), os.lstat(os.path.join(basis, e)).st_size))
        except OSError:
            pass
        uit.append({"map": os.path.relpath(pad, view) if pad != view else ".", "pad": pad, "wijst_naar": os.path.realpath(pad),
                    "via": os.path.relpath(via, view) if via else ".", "rollen": sorted(rollen), "klasse": "artefact",
                    "status": status, "soort": "map", "bewezen": None, "schrijvers": schrijvers, "bewijs": bew})
    return uit


def restanten_op_nas(spelmap, dropin_paren=(), max_diepte=3):
    """KLASSE (b), restanten: onze artefacten die al in de spelmap op de NAS staan (opruimen is aan de eigenaar, niet aan dit
    script). Zoekt op naam (RavenGun*, nl.studiodamon.*, *.ravengun-tmp, rg-*) tot diepte 3 (zware assetmappen worden overgeslagen)
    en vergelijkt drop-inbestanden (dropin_paren: lijst (bron, pad op de NAS)) op sha256. Leest alleen."""
    uit = []
    if not spelmap or not os.path.isdir(spelmap):
        return uit

    def loop(d, n):
        try:
            namen = sorted(os.listdir(d))
        except OSError:
            return
        for e in namen:
            p = os.path.join(d, e)
            try:
                st = os.lstat(p)
            except OSError:
                continue
            if ONZE_NAMEN.search(e):
                uit.append({"map": os.path.relpath(p, spelmap), "pad": p, "wijst_naar": p, "via": ".", "rollen": ["aanwezig-op-nas"],
                            "klasse": "artefact", "status": "restant", "soort": "bestand", "bewezen": True,
                            "schrijvers": ["naam wijst op een artefact van ons (plugin, drop-in of hulpbestand)"],
                            "bewijs": [(e, _datum(st), st.st_size)]})
            if os.path.isdir(p) and not os.path.islink(p) and n < max_diepte and e.lower() not in HEAVY:
                loop(p, n + 1)

    loop(spelmap, 1)
    # staat onze plugin of config in BepInEx/ op de NAS, dan zijn de bestanden die BepInEx daar sindsdien schreef (config, cache,
    # LogOutput.log; nieuwer dan 2026-10-09) ook van onze run
    if any(x["map"].startswith("BepInEx" + os.sep) for x in uit):
        bekend = {x["map"] for x in uit}
        bep = os.path.join(spelmap, "BepInEx")
        for rel in ("LogOutput.log", "config", "cache", "plugins"):
            kand = os.path.join(bep, rel)
            paden = [kand] if os.path.isfile(kand) else ([os.path.join(kand, e) for e in sorted(os.listdir(kand))] if os.path.isdir(kand) else [])
            for pth in paden:
                try:
                    st = os.lstat(pth)
                except OSError:
                    continue
                r = os.path.relpath(pth, spelmap)
                if r not in bekend and os.path.isfile(pth) and st.st_mtime >= EPOCH:
                    uit.append({"map": r, "pad": pth, "wijst_naar": pth, "via": ".", "rollen": ["aanwezig-op-nas"], "klasse": "artefact",
                                "status": "restant", "soort": "bestand", "bewezen": True,
                                "schrijvers": ["geschreven door BepInEx tijdens een run met onze plugin"],
                                "bewijs": [(os.path.basename(pth), _datum(st), st.st_size)]})
    for src, nasbestand in dropin_paren:
        try:
            if os.path.isfile(src) and os.path.isfile(nasbestand) and sha256(src) == sha256(nasbestand):
                st = os.stat(nasbestand)
                uit.append({"map": os.path.relpath(nasbestand, spelmap), "pad": nasbestand, "wijst_naar": nasbestand, "via": ".",
                            "rollen": ["aanwezig-op-nas"], "klasse": "artefact", "status": "restant", "soort": "bestand",
                            "bewezen": True, "schrijvers": ["identiek aan onze drop-in %s" % os.path.basename(src)],
                            "bewijs": [(os.path.basename(nasbestand), _datum(st), st.st_size)]})
        except OSError:
            pass
    return uit


def startplan(profiel):
    """launch-direct --plan --json (start niets). Geeft het plan of None."""
    try:
        r = subprocess.run([sys.executable, "-I", os.path.join(REPO, "scripts", "launch-direct.py"), "--plan", "--json", profiel],
                           capture_output=True, text=True, timeout=120)
        return json.loads(r.stdout) if r.returncode == 0 and r.stdout.strip() else None
    except (OSError, ValueError, subprocess.TimeoutExpired):
        return None


def spelmap_op_nas(pad, nas=None):
    """De spelmap op de NAS (NAS/<verzameling>/<game>) waar het (gevolgde) pad onder valt, of None."""
    nas = os.path.realpath(nas or NAS)
    real = os.path.realpath(pad)
    if not onder(real, nas) or real == nas:
        return None
    delen = os.path.relpath(real, nas).split(os.sep)
    return os.path.join(nas, *delen[:2]) if len(delen) >= 2 else os.path.join(nas, delen[0])


def dropin_info(plan):
    """(doelpaden, (bron, relatief pad)-lijst, base) uit het startplan; leeg als er geen drop-ins zijn."""
    di = (plan or {}).get("dropins") or {}
    bestanden = di.get("files") or []
    base = di.get("base")
    return [f["dest"] for f in bestanden], [(f["src"], os.path.relpath(f["dest"], base)) for f in bestanden if base], base


def scan_alles(namen=None, plan=True, nas=None):
    """Scan alle views. Geeft lijst {view, soort, plan, bevindingen}; elke bevinding heeft klasse "ontwerp" of "artefact"."""
    uit = []
    for soort, wortel in (("tp", VIEWS), ("aagames", VIEWS_AAGAMES)):
        if not os.path.isdir(wortel):
            continue
        for n in sorted(os.listdir(wortel)):
            view = os.path.join(wortel, n)
            if not os.path.isdir(view) or (namen and n not in namen):
                continue
            p = startplan(n) if plan else None
            doelen, paren, base = dropin_info(p)
            if soort == "aagames":
                # de aagames-route plaatst de drop-ins in de view-wortel (daar staat Game.exe): zelfde relatieve paden als het plan
                doelen = [os.path.join(view, r) for _, r in paren]
                spel = os.path.join(view, "Game.exe")
                b = ontwerp_view(view, nas, spel=spel, cwd=view, aagames=True)
                b += artefacten_view(view, nas, aagames=True, dropin_doelen=doelen, gedekt=False)
                nasdir = spelmap_op_nas(spel, nas)
                b += restanten_op_nas(nasdir, [(src, os.path.join(os.path.dirname(os.path.realpath(spel)), r)) for (src, r) in paren])
                uit.append({"view": n, "soort": soort, "plan": "n.v.t. (play-unity.sh)", "bevindingen": b})
                continue
            if p:
                workdir = tp_pad_naar_unix((p.get("profile_env") or {}).get("TP_GAME_WORKING_DIRECTORY")
                                           or (p.get("launch_env") or {}).get("TP_GAME_WORKING_DIRECTORY"))
                b = ontwerp_view(view, nas, spel=p["game"], cwd=p.get("cwd"), workdir=workdir)
                b += artefacten_view(view, nas, dropin_doelen=doelen, gedekt=True)
                nasdir = spelmap_op_nas(p["game"], nas)
                nasexe = os.path.dirname(os.path.realpath(p["game"]))
                b += restanten_op_nas(nasdir, [(src, os.path.join(nasexe, r)) for (src, r) in paren])
                uit.append({"view": n, "soort": soort, "plan": "plan ok: %s" % p.get("emulator_type", "?"), "bevindingen": b})
            else:
                mappen = exe_uit_gameini(n, view) or zoek_exe_mappen(view)
                b = ontwerp_view(view, nas, exe_mappen=mappen)
                b += artefacten_view(view, nas)
                nasdir = spelmap_op_nas(mappen[0], nas) if mappen else None
                b += restanten_op_nas(nasdir)
                uit.append({"view": n, "soort": soort, "plan": "geen startplan (exe-map uit game.ini/geschat)", "bevindingen": b})
    g = globale_artefacten(nas)
    uit.append({"view": "(globaal)", "soort": "globaal", "plan": "gepatchte binaries: %s" % PATCHED_ROOT, "bevindingen": g})
    return uit


def globale_artefacten(nas=None, patched_root=None):
    """KLASSE (b), globaal: de map voor gepatchte binaries (launch-direct: patched_root) hoort lokaal te zijn; patchbestanden komen
    daar, nooit naast het origineel op de NAS."""
    patched_root = patched_root or PATCHED_ROOT
    if not onder(patched_root, nas or NAS):
        return []
    return [{"map": patched_root, "pad": patched_root, "wijst_naar": os.path.realpath(patched_root), "via": ".",
             "rollen": ["gepatcht"], "klasse": "artefact", "status": "open", "soort": "map", "bewezen": None,
             "schrijvers": ["launch-direct: gepatchte kopieen van binaries"], "bewijs": []}]


def ernstig(b):
    """Een echte bevinding: klasse (b), onze artefacten, zonder bescherming. Klasse (a) (ontwerp), 'gedekt' en 'restant' tellen niet."""
    return b.get("klasse") == "artefact" and b.get("status") == "open"


def _kolom(b):
    return b["wijst_naar"].replace(NAS, "NAS")


def md_tabellen(res):
    """Twee markdown-tabellen: klasse (a) ontwerp en klasse (b) artefacten."""
    a = ["| view | map | wijst naar | wie kan er schrijven | wat | bewijs op de NAS (datum) | status |",
         "|---|---|---|---|---|---|---|"]
    b = ["| view | plek | wijst naar | rol | status | wie/wat | bewijs op de NAS (datum) |", "|---|---|---|---|---|---|---|"]
    gezien = {}   # restanten op dezelfde NAS-plek (twee views op dezelfde spelmap) maar een keer tonen
    for v in res:
        for x in v["bevindingen"]:
            if x["klasse"] == "artefact" and x["status"] == "restant":
                if x["wijst_naar"] in gezien:
                    gezien[x["wijst_naar"]].append(v["view"] + " (" + v["soort"] + ")")
                    continue
                gezien[x["wijst_naar"]] = [v["view"] + " (" + v["soort"] + ")"]
                x = dict(x, _rij=gezien[x["wijst_naar"]])
            bew = "<br>".join("`%s` (%s)" % (r, d) for r, d, _ in x["bewijs"]) or "geen nieuwe bestanden gevonden"
            if x["klasse"] == "ontwerp":
                a.append("| %s | `%s` (rol: %s; via `%s`) | `%s` | %s | %s | %s | %s |" % (
                    v["view"], x["map"], ", ".join(x["rollen"]), x["via"], _kolom(x),
                    x["schrijvers"][0] if x["schrijvers"] else "-", "; ".join(x["schrijvers"][1:]) or "-", bew, x["status"]))
            else:
                b.append("| %s | `%s` (via `%s`) | `%s` | %s | **%s** | %s | %s |" % (
                    " + ".join(x["_rij"]) if "_rij" in x else v["view"], x["map"], x["via"], _kolom(x), ", ".join(x["rollen"]), x["status"], "; ".join(x["schrijvers"]) or "-", bew))
    return "\n".join(a), "\n".join(b)


def md_tabel(res):
    a, b = md_tabellen(res)
    return "### Klasse (a): ontwerp\n\n" + a + "\n\n### Klasse (b): onze artefacten\n\n" + b


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("views", nargs="*")
    ap.add_argument("--md", action="store_true")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--geen-plan", action="store_true")
    a = ap.parse_args()
    res = scan_alles(a.views or None, plan=not a.geen_plan)
    if a.json:
        print(json.dumps(res, indent=1, ensure_ascii=False))
    elif a.md:
        print(md_tabel(res))
    else:
        for v in res:
            ont = [x for x in v["bevindingen"] if x["klasse"] == "ontwerp"]
            art = [x for x in v["bevindingen"] if x["klasse"] == "artefact"]
            open_ = [x for x in art if ernstig(x)]
            print("%-18s %-8s %s: ontwerp (a): %d, artefacten (b): %d (open: %d)" % (v["view"], v["soort"], v["plan"], len(ont), len(art), len(open_)))
            for x in art:
                print("    (b) %-9s %-34s -> %s  [%s]" % (x["status"].upper(), x["map"], _kolom(x), ", ".join(x["rollen"])))
                for r, d, n in x["bewijs"][:6]:
                    print("            bewijs: %s (%s, %d B)" % (r, d, n))
            for x in ont:
                print("    (a) ontwerp  %-34s -> %s  [%s]" % (x["map"], _kolom(x), ", ".join(x["rollen"])))
        n_open = sum(1 for v in res for x in v["bevindingen"] if ernstig(x))
        print("\nKlasse (b) met status open: %d. Klasse (a) is ontwerp en telt niet mee (%s)." % (n_open, BESLUIT))
    sys.exit(1 if any(ernstig(x) for v in res for x in v["bevindingen"]) else 0)


if __name__ == "__main__":
    main()
