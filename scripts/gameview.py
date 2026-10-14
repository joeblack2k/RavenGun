# SPDX-License-Identifier: GPL-3.0-or-later
"""De view van een game (docs/per-game-patches.md, "De view"): een lokale map ~/.local/share/ravengun/runtime/views/<Profiel> met
symlinks naar de gamemap (meestal op de NAS), met echte lokale mappen waar de game of wij schrijven: de map van de exe en elke map
erboven tot de wortel (daar komt teknoparrot.ini), BepInEx/ (lokaal gekopieerd) en de lege mappen van --empty-dir /
[Launch] ViewEmptyDirs. Er wordt niets van de game gekopieerd (op BepInEx/ na) en de bron blijft ongewijzigd.

Gebruikt door scripts/tp-add-game.py --view (met de hand) en scripts/launch-direct.py ([Launch] View = auto: vanzelf voor een game op de
NAS). De C++-tweeling is src/ravengun/launch/gameview.*; tests/test_gameview.py vergelijkt de bomen van beide op een nep-NAS.

Nooit op de NAS schrijven: elke stap (map maken, link zetten of weghalen, bestand kopieren) controleert eerst of de map waarin hij
schrijft lokaal is (on_nas, gevolgd); zo niet, dan stopt de opbouw met ViewFout. Na de opbouw controleert schrijf_fouten() onze
schrijfpaden (zoals pruef_schrijfpaden in scripts/aagames-setup.py)."""
import os
import shutil

# BepInEx/ is ONS artefact (onze plugin met zijn config, cache en LogOutput.log): die boom hoort lokaal in de view, nooit als symlink
# naar de NAS (gemeten 2026-10-11, Rabbids Hollywood: BepInEx/ op de NAS beschreven via zo'n view). De eigen bestanden van de game
# (saves, instellingen, kleine .ini/.json/.conf, NVRAM, dxvk-cache) blijven symlinks: de game woont op de NAS en schrijft daar zelf
# (besluit van de eigenaar, 2026-10-12).
LOKAAL_MAPPEN = {'bepinex'}


class ViewFout(Exception):
    """De view kan niet (veilig) gemaakt worden; de melding zegt waarom."""


def on_nas(path, home=None):
    """True als het (gevolgde) pad op een netwerkschijf ligt: ~/mnt of cifs/smb/nfs/sshfs (zelfde regel als launch-direct.py en de app)."""
    real = os.path.realpath(path)
    home = home or os.path.expanduser('~')
    if real.startswith(os.path.join(home, 'mnt') + '/'):
        return True
    try:
        best, fstype = '', ''
        with open('/proc/self/mounts') as f:
            for line in f:
                parts = line.split()
                if len(parts) >= 3:
                    mp = parts[1].replace('\\040', ' ')
                    if (real == mp or real.startswith(mp.rstrip('/') + '/')) and len(mp) > len(best):
                        best, fstype = mp, parts[2]
        return fstype in ('cifs', 'smb3', 'smbfs', 'nfs', 'nfs4', 'fuse.sshfs')
    except OSError:
        return False


def _nooit(_pad):
    return False


class _Wacht:
    """Controle voor elke schrijfstap: de map erboven moet lokaal zijn (gevolgd)."""

    def __init__(self, on_nas):
        self.on_nas = on_nas or _nooit

    def __call__(self, doel):
        ouder = os.path.dirname(os.path.abspath(doel))
        if self.on_nas(ouder):
            raise ViewFout('%s: de map erboven komt uit op %s en dat is de NAS; daar wordt nooit geschreven'
                           % (doel, os.path.realpath(ouder)))


def kopieer_lokaal(src, dst, on_nas=None):
    """Maak dst een echte map met echte kopieen van src (recursief). Bestaande lokale bestanden blijven staan; een symlink
    op die plek wordt vervangen (alleen de link zelf wordt verwijderd, nooit wat erachter staat)."""
    w = _Wacht(on_nas)
    if os.path.islink(dst):
        w(dst); os.unlink(dst)
    if not os.path.isdir(dst):
        w(dst); os.makedirs(dst, exist_ok=True)
    for e in os.listdir(src):
        sp, dp = os.path.join(src, e), os.path.join(dst, e)
        if os.path.islink(dp):
            w(dp); os.unlink(dp)
        if os.path.isdir(sp):
            kopieer_lokaal(sp, dp, on_nas)
        elif not os.path.lexists(dp):
            w(dp); shutil.copyfile(sp, dp); shutil.copymode(sp, dp)


def link_tree(src, dst, empties, top=True, on_nas=None):
    """Link elke ingang van src in dst; een pad uit `empties` (lijst van padonderdelen) wordt een lege map, en de mappen
    erlangs worden echte mappen met links voor de rest (alleen lokaal; de bron blijft ongewijzigd). Op het hoogste niveau
    (top) wordt BepInEx/ lokaal gekopieerd in plaats van gelinkt. Een link op de plek van een map die echt moet worden, gaat eerst
    weg (anders kwamen de links erin via die link op de NAS terecht)."""
    w = _Wacht(on_nas)
    if not top and os.path.islink(dst):
        w(dst); os.unlink(dst)
    if not os.path.isdir(dst):
        w(dst); os.makedirs(dst, exist_ok=True)
    first = {e[0] for e in empties if e}
    for e in os.listdir(src):
        sp, dp = os.path.join(src, e), os.path.join(dst, e)
        if e in first:
            sub = [x[1:] for x in empties if x and x[0] == e]
            if [] in sub:      # dit onderdeel zelf wordt leeg
                if os.path.islink(dp):
                    w(dp); os.unlink(dp)
                if not os.path.isdir(dp):
                    w(dp); os.makedirs(dp, exist_ok=True)
            else:
                link_tree(sp, dp, [x for x in sub if x], False, on_nas)
        elif top and e.lower() in LOKAAL_MAPPEN and os.path.isdir(sp):
            kopieer_lokaal(sp, dp, on_nas)
        else:
            if os.path.islink(dp):                    # een vorige keer
                w(dp); os.unlink(dp)
            if not os.path.lexists(dp):               # een echte map van een vorige keer (bijv. een gematerialiseerde drop-in-map) blijft staan
                w(dp); os.symlink(sp, dp)


def maak_keten_lokaal(src, view, rel_dir, on_nas=None):
    """De map van de exe, en elke map erboven tot aan de wortel van de view, wordt een ECHTE map met symlinks naar de inhoud
    (in plaats van een symlink naar de NAS). Anders komt alles wat naast de exe moet staan (teknoparrot.ini, een drop-in met To = .,
    docs/dropins.md) via die symlink op de NAS uit en kan het niet geplaatst worden; dat moest bij Luigi's Mansion (exe/x64/VACUUM.exe)
    met de hand. Opnieuw te draaien: de links worden ververst en echte bestanden van een vorige keer (de drop-ins zelf) blijven staan.
    De bron wordt alleen gelezen. Geeft de mappen terug die lokaal zijn gemaakt of ververst."""
    w = _Wacht(on_nas)
    gedaan = []
    sp, dp = src, view
    for deel in [d for d in rel_dir.split(os.sep) if d and d != '.']:
        sp, dp = os.path.join(sp, deel), os.path.join(dp, deel)
        if os.path.islink(dp):
            w(dp); os.unlink(dp)
        if not os.path.isdir(dp):
            w(dp); os.makedirs(dp, exist_ok=True)
        for e in os.listdir(dp):
            if os.path.islink(os.path.join(dp, e)):
                w(os.path.join(dp, e)); os.unlink(os.path.join(dp, e))
        for e in os.listdir(sp):
            if not os.path.lexists(os.path.join(dp, e)):
                w(os.path.join(dp, e)); os.symlink(os.path.join(sp, e), os.path.join(dp, e))
        gedaan.append(dp)
    return gedaan


def schrijf_fouten(view, rel, empties, on_nas=None):
    """Onze schrijfpaden in de view die op de NAS uitkomen (leeg = goed): de view zelf, de mappen op weg naar de exe (daar komt
    teknoparrot.ini), BepInEx/ met alles erin en de lege mappen."""
    on_nas = on_nas or _nooit
    if on_nas(view):
        return ['de view %s ligt op de NAS' % view]
    if os.path.islink(view):
        return ['de view %s is een symlink naar %s' % (view, os.path.realpath(view))]
    fouten = []

    def check(p):
        if os.path.islink(p) and on_nas(p):
            fouten.append('%s -> %s' % (p, os.path.realpath(p)))
        elif on_nas(p):
            fouten.append('%s ligt op de NAS' % p)

    def boom(p):
        check(p)
        if os.path.isdir(p) and not os.path.islink(p):
            for e in sorted(os.listdir(p)):
                boom(os.path.join(p, e))

    d = view
    for deel in [x for x in os.path.dirname(rel).split('/') if x and x != '.']:
        d = os.path.join(d, deel)
        if os.path.lexists(d):
            check(d)
    for e in sorted(os.listdir(view)) if os.path.isdir(view) else ():
        if e.lower() in LOKAAL_MAPPEN:
            boom(os.path.join(view, e))
    for e in empties:
        p = os.path.join(view, e.strip('/'))
        if os.path.lexists(p):
            check(p)
    return fouten


def maak_view(gdir, view, rel, empties=(), elfldr2=None, on_nas=None, log=None):
    """De view zoals tp-add-game.py --view hem maakt: links en lege mappen van een vorige keer weg, alles linken (BepInEx/ lokaal,
    `empties` leeg), de keten naar de exe echte mappen, en ElfLdr2 (een link naar de lader van TeknoParrot) als `elfldr2` is gegeven.
    gdir = de gamemap die de view spiegelt, rel = de exe ten opzichte van gdir. Geeft het pad van de exe in de view."""
    log = log or (lambda m: None)
    nas = on_nas or _nooit
    if nas(view):
        raise ViewFout('de view %s komt uit op %s en dat is de NAS; een view is altijd een lokale map' % (view, os.path.realpath(view)))
    if os.path.islink(view):
        raise ViewFout('de view %s is zelf een symlink (naar %s)' % (view, os.path.realpath(view)))
    w = _Wacht(on_nas)
    if not os.path.isdir(view):
        w(view); os.makedirs(view, exist_ok=True)
    for e in os.listdir(view):
        pth = os.path.join(view, e)
        if os.path.islink(pth):
            w(pth); os.unlink(pth)
        elif os.path.isdir(pth) and e != 'TeknoParrot' and not os.listdir(pth):   # lege mappen van een vorige keer
            w(pth); os.rmdir(pth)
    link_tree(gdir, view, [e.strip('/').split('/') for e in empties], on_nas=on_nas)
    for d in maak_keten_lokaal(gdir, view, os.path.dirname(rel), on_nas):
        log('view: %s/ is een echte map met symlinks (de map van de exe hoort lokaal te zijn)' % os.path.relpath(d, view))
    if elfldr2 and not os.path.lexists(os.path.join(view, 'ElfLdr2')):
        w(os.path.join(view, 'ElfLdr2')); os.symlink(elfldr2, os.path.join(view, 'ElfLdr2'))
    fouten = schrijf_fouten(view, rel, empties, on_nas)
    if fouten:
        raise ViewFout('schrijfpaden in de view die op de NAS uitkomen (dat mag nooit):\n  ' + '\n  '.join(fouten))
    log('view: %s gemaakt (links naar %s)' % (view, gdir))
    return os.path.join(view, rel)


def ververs_view(gdir, view, rel, empties=(), on_nas=None, log=None):
    """Een bestaande view alleen verversen: in de wortel en in de mappen op weg naar de exe (echte mappen) een link voor wat er in
    de gamemap bij is gekomen, en links die naar een verdwenen bestand in de gamemap wijzen weg. Echte bestanden en mappen blijven
    staan (ook lege mappen en BepInEx/)."""
    log = log or (lambda m: None)
    nas = on_nas or _nooit
    if nas(view):
        raise ViewFout('de view %s komt uit op %s en dat is de NAS' % (view, os.path.realpath(view)))
    w = _Wacht(on_nas)
    paren = [(view, gdir)]
    for deel in [x for x in os.path.dirname(rel).split('/') if x and x != '.']:
        paren.append((os.path.join(paren[-1][0], deel), os.path.join(paren[-1][1], deel)))
    bij = weg = 0
    for i, (vd, sd) in enumerate(paren):
        if os.path.islink(vd) or not os.path.isdir(vd):
            break                                   # geen echte map (meer): wat eronder zit is niet van ons
        try:
            namen = sorted(os.listdir(sd))
        except OSError:
            namen = []
        for e in namen:
            if i == 0 and e.lower() in LOKAAL_MAPPEN:
                continue
            dp = os.path.join(vd, e)
            if not os.path.lexists(dp):
                w(dp); os.symlink(os.path.join(sd, e), dp)
                bij += 1
        for e in sorted(os.listdir(vd)):
            dp = os.path.join(vd, e)
            if os.path.islink(dp) and not os.path.exists(dp) and os.readlink(dp).startswith(sd + '/'):
                w(dp); os.unlink(dp)
                weg += 1
    if bij or weg:
        log('view: %s ververst (%d links bij, %d verdwenen links weg)' % (view, bij, weg))
    # Geen schrijf_fouten() hier: een bestaande view blijft zoals hij is (ook een met de hand gemaakte waarin de map van de exe nog een
    # link is; de drop-ins maken die zelf lokaal en teknoparrot.ini wordt dan niet op de NAS geschreven). Verversen schrijft zelf alleen
    # links in echte lokale mappen (_Wacht).
    return os.path.join(view, rel)


def view_instelling(view, empty_dirs):
    """[Launch] View en ViewEmptyDirs: (stand, omhoog, lege mappen). View: auto (leeg; een view als de game op de NAS staat, met de
    wortel waar [Identify] Path begint), off (de oude route: rechtstreeks van de NAS) of een getal N (altijd een view, de wortel N
    mappen boven die van de exe; --view-up N). ViewEmptyDirs: paden ten opzichte van de view-wortel, gescheiden door komma of
    puntkomma (--empty-dir). ValueError bij een ongeldige waarde."""
    v = ''.join((view or '').split()).lower()
    up = 0
    if v in ('', 'auto'):
        stand = 'auto'
    elif v in ('off', 'false', 'no'):
        stand = 'off'
    elif len(v) <= 2 and v.isdigit() and v.isascii():
        stand, up = 'levels', int(v)
    else:
        raise ValueError('[Launch] View = %s (auto, off, of het aantal mappen boven de map van de exe waar de view begint)' % view)
    lege = []
    for t in (empty_dirs or '').replace(';', ',').split(','):
        t = t.strip().strip('/')
        if not t:
            continue
        if '..' in [d for d in t.split('/')]:
            raise ValueError("[Launch] ViewEmptyDirs = %s: '%s' gaat buiten de view" % (empty_dirs, t))
        lege.append(t)
    return stand, up, lege
