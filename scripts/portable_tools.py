#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Hulpmiddelen van scripts/make-portable.sh (draai ze met python3 -I).

links     alle koppelingen in de nieuwe map in één doorgang herschrijven én controleren:
          - absoluut naar de runtime, de installatie, de configuratie of de TeknoParrot-Proton
            (ook als het doel precies die map zelf is): relatieve koppeling naar de kopie in de map;
          - absoluut naar elders in ~/.local of de repo: de inhoud uitpakken (-L voor een bestand, een
            echte kopie voor een map, ook via een keten van mapkoppelingen); koppelingen in zo'n kopie
            komen in dezelfde doorgang aan de beurt. Een map wordt alleen LEEG gemaakt als zijn doel op de
            toegestane lijst staat (--empty-ok, bijvoorbeeld de Steam-appcache); anders kopiëren of falen;
          - relatief: moet binnen de map blijven, en het doel moet in de map bestaan (per padonderdeel met lstat, zonder een
            koppeling onderweg te volgen; zo faalt een koppeling naar het overgeslagen bench-captures of halo-overlay);
          - absoluut naar buiten de thuismap-indeling (de NAS): blijft staan, zonder het doel aan te raken
            (alleen lexicale controle; een realpath zou per koppeling een netwerkaanroep zijn).
          Uitpakken controleert grootte (--max-extract) en vrije ruimte, ook voor een bestand; grootte meten stopt zodra de grens
          over is en volgt nooit een koppeling (een NAS-koppeling in de boom wordt niet aangeraakt).
          Gescand wordt alleen de boom die gebouwd wordt (bin/, lib/, share/ en eventueel de nieuwe User);
          een oude User/, User.new of User.old blijft ongemoeid. Exit 1 bij elke overtreding.
whiteouts overlay-toestand na een rsync -aX naar de kopie brengen. Een rsync zonder rootrechten slaat
          char-devices over, en dat zijn precies de whiteouts van de schrijflagen (kernel-overlay in een
          user-namespace via bwrap --overlay: whiteout = char-device 0:0, mappen en bestanden dragen
          user.overlay.*-xattrs). Maakt de ontbrekende whiteouts aan (mknod 0:0 mag zonder rechten) en
          controleert per pad dat ze er staan en dat de user.overlay.*-waarden gelijk zijn; exit 1 als dat niet lukt.
          Met --scope auto alleen waar de overlays leven (een boom writelayer, een top met user.overlay.*-xattrs, en de
          mappen 'upper' tot drie niveaus diep, zoals native/<Game>/<n>/upper), niet de ~100k inodes van proton/prefixen.
          Een draaiende game verandert de bron na de rsync: wat alleen in de bron staat (een map of pad dat in de kopie
          ontbreekt, een whiteout die tussen scannen en stat verdwijnt) wordt niet vergeleken maar gemeld; een verschil op
          een pad dat in beide staat is een fout, of een waarschuwing als rsync met 24 (bestanden verdwenen) eindigde.
busy      exit 1 als een proces dat we kunnen lezen een van de mappen gebruikt (mount, ook als bind onder een ander pad in een
          sandbox; werkmap; open of gemapt bestand). Voor de wissel van User/ bij --reset-user.
"""
import argparse
import fnmatch
import os
import shutil
import stat
import subprocess
import sys


def inside(p, root):
    root = root.rstrip("/")
    return p == root or p.startswith(root + "/")


def tree_size(path, cap=None):
    """Grootte van de boom (lstat, koppelingen worden niet gevolgd: een NAS-koppeling erin wordt niet aangeraakt). Stopt zodra
    cap overschreden is; de uitkomst is dan alleen 'groter dan cap'."""
    total = 0
    stack = [path]
    while stack:
        d = stack.pop()
        try:
            with os.scandir(d) as it:
                for e in it:
                    try:
                        if e.is_dir(follow_symlinks=False):
                            stack.append(e.path)
                        else:
                            total += e.stat(follow_symlinks=False).st_size
                    except OSError:
                        pass
                    if cap is not None and total > cap:
                        return total
        except OSError:
            pass
    return total


def walk_nofollow(top):
    """(map, [entries]) voor elke map onder top, zonder mapkoppelingen te volgen."""
    stack = [top]
    while stack:
        d = stack.pop()
        try:
            with os.scandir(d) as it:
                entries = list(it)
        except OSError:
            continue
        yield d, entries
        for e in entries:
            try:
                if e.is_dir(follow_symlinks=False):
                    stack.append(e.path)
            except OSError:
                pass


def free_bytes(d):
    try:
        st = os.statvfs(d)
        return st.f_bavail * st.f_frsize
    except OSError:
        return None


FREE_MARGIN = 64 * 1024 * 1024    # zoveel moet er na het uitpakken nog vrij blijven


class Linker:
    def __init__(self, a):
        self.dest = a.dest.rstrip("/")
        self.actual_user = a.actual_user.rstrip("/")
        self.userdir = a.user.rstrip("/")
        self.home_roots = [r.rstrip("/") for r in a.home_root]
        self.empty_ok = a.empty_ok
        self.max_extract = a.max_extract
        self.scan_user = a.scan_user
        # (doel in de thuismap-indeling, eindpad in de map), in deze volgorde geprobeerd
        u = self.userdir
        self.maps = [(a.runtime.rstrip("/"), u + "/share/ravengun"),
                     (a.install.rstrip("/"), self.dest),
                     (a.config.rstrip("/"), u + "/config/ravengun"),
                     (a.tui_proton.rstrip("/"), u + "/share/ravengun/proton")]
        self.bad = []
        self.total = self.rewritten = self.extracted = self.outside = 0
        self.emptied = []
        self.restore = {}      # map -> oorspronkelijke mode, zolang we er tijdelijk in mogen schrijven

    # --- paden: eindpad (waar User/ straks staat) tegenover het huidige pad (User.new) -------------
    def actual(self, p):
        if p == self.userdir:
            return self.actual_user
        if p.startswith(self.userdir + "/"):
            return self.actual_user + p[len(self.userdir):]
        return p

    def final(self, p):
        if p == self.actual_user:
            return self.userdir
        if p.startswith(self.actual_user + "/"):
            return self.userdir + p[len(self.actual_user):]
        return p

    def map_target(self, t):
        """Het eindpad van de kopie van doel t, of None. Ook als een map in de runtime hernoemd is
        (TeknoParrotUi-... -> .verborgen) wijzen we naar de juiste plek, met het hele restpad."""
        nt = None
        for root, base in self.maps:
            if inside(t, root):
                rest = t[len(root):].lstrip("/")
                nt = base + "/" + rest if rest else base
                break
        if nt is None:
            return None
        if os.path.lexists(self.actual(nt)):
            return nt
        d, rest = os.path.split(nt)
        while d not in ("/", ".", "", self.dest):
            if os.path.lexists(self.actual(d + ".verborgen/" + rest)):
                return d + ".verborgen/" + rest
            rest = os.path.basename(d) + "/" + rest
            d = os.path.dirname(d)
        return None

    # --- schrijven in mappen die (nog) niet schrijfbaar zijn ----------------------------------------
    def writable(self, d):
        if d in self.restore or os.access(d, os.W_OK | os.X_OK):
            return
        st = os.lstat(d)
        os.chmod(d, st.st_mode | 0o700)
        self.restore[d] = stat.S_IMODE(st.st_mode)

    def restore_modes(self):
        for d in sorted(self.restore, key=len, reverse=True):
            try:
                os.chmod(d, self.restore[d])
            except OSError:
                pass

    def fail(self, lf, t, why):
        self.bad.append((lf, t, why))

    def exists_in_dest(self, final_path):
        """Bestaat final_path (eindpad in de map) op schijf? Per padonderdeel met lstat; komt er onderweg een koppeling, dan
        stoppen we en is het goed: die koppeling wordt zelf gecontroleerd (en zo raken we nooit de NAS)."""
        rel = os.path.relpath(final_path, self.dest)
        if rel == ".":
            return True
        cur = self.dest
        parts = rel.split("/")
        for i, part in enumerate(parts):
            cur = cur + "/" + part
            try:
                st = os.lstat(self.actual(cur))
            except (FileNotFoundError, NotADirectoryError):
                return False
            except OSError:
                return True     # niet te zien (rechten): niet afkeuren
            if stat.S_ISLNK(st.st_mode) and i < len(parts) - 1:
                return True
        return True

    def room_for(self, p, size):
        free = free_bytes(os.path.dirname(p))
        return free is None or free >= size + FREE_MARGIN

    def set_link(self, p, rel):
        d = os.path.dirname(p)
        self.writable(d)
        tmp = p + ".rgln"
        if os.path.lexists(tmp):
            os.unlink(tmp)
        os.symlink(rel, tmp)
        os.replace(tmp, p)

    # --- één koppeling -------------------------------------------------------------------------------
    def link(self, p, chain=()):
        t = os.readlink(p)
        lf = self.final(p)
        self.total += 1
        if not os.path.isabs(t):
            probe = os.path.normpath(os.path.join(os.path.dirname(lf), t))
            if not inside(probe, self.dest):
                self.fail(lf, t, "relatieve koppeling wijst uit de map")
            elif not self.exists_in_dest(probe):
                self.fail(lf, t, "relatieve koppeling naar iets dat niet in de map staat (overgeslagen of ontbrekend)")
            return
        nt = os.path.normpath(t)
        if inside(nt, self.dest):
            # al in de map, maar absoluut: relatief maken zodat de map kan verhuizen
            if os.path.lexists(self.actual(nt)):
                self.set_link(p, os.path.relpath(nt, os.path.dirname(lf)))
                self.rewritten += 1
            else:
                self.fail(lf, t, "wijst naar de map zelf, maar het doel bestaat niet")
            return
        if not any(inside(nt, r) for r in self.home_roots):
            self.outside += 1      # de NAS e.d.: ongemoeid, doel niet aangeraakt
            return
        m = self.map_target(nt)
        if m:
            self.set_link(p, os.path.relpath(m, os.path.dirname(lf)))
            self.rewritten += 1
            return
        self.extract(p, lf, t, nt, chain)

    def extract(self, p, lf, t, nt, chain):
        """Wat naar ~/.local of de repo wijst en niet mee gekopieerd is: de inhoud erin zetten."""
        try:
            real = os.path.realpath(nt)
            st = os.stat(real)
        except OSError:
            self.fail(lf, t, "wijst nog naar de thuismap-indeling en het doel bestaat niet")
            return
        tmp = p + ".rgtmp"
        self.writable(os.path.dirname(p))
        if os.path.lexists(tmp):
            shutil.rmtree(tmp) if os.path.isdir(tmp) and not os.path.islink(tmp) else os.unlink(tmp)
        if not stat.S_ISDIR(st.st_mode):
            if st.st_size > self.max_extract:
                self.fail(lf, t, "bestand van {} MB is te groot om uit te pakken (grens {} MB)".format(
                    st.st_size // 1048576, self.max_extract // 1048576))
                return
            if not self.room_for(p, st.st_size):
                self.fail(lf, t, "te weinig ruimte om {} MB uit te pakken".format(st.st_size // 1048576))
                return
            try:
                shutil.copy2(real, tmp)     # inhoud, rechten en tijden; geen cp per bestand
            except OSError as e:
                if os.path.lexists(tmp):
                    os.unlink(tmp)
                self.fail(lf, t, "uitpakken mislukt: {}".format(e.strerror or e))
                return
            os.replace(tmp, p)
            self.extracted += 1
            return
        # een map
        if any(fnmatch.fnmatchcase(x, pat) for pat in self.empty_ok for x in (nt, real)):
            os.unlink(p)
            os.mkdir(p)
            self.emptied.append(lf)
            # geen grootte: de appcache (~600 MB) doorlopen alleen voor een getal is zonde
            print("  cache-koppeling (toegestaan) vervangen door lege map: {}".format(lf))
            return
        if real in chain or len(chain) >= 8:
            self.fail(lf, t, "mapkoppeling die in zichzelf uitkomt of te diep geneste mapkoppelingen")
            return
        size = tree_size(real, self.max_extract)
        if size > self.max_extract:
            self.fail(lf, t, "map van meer dan {} MB is te groot om uit te pakken; zet hem op de "
                      "toegestane lijst (RG_PORTABLE_EMPTY_OK) als de eigenaar hem zelf weer vult, of los "
                      "de koppeling op".format(self.max_extract // 1048576))
            return
        if not self.room_for(p, size):
            self.fail(lf, t, "te weinig ruimte om de map van {} MB uit te pakken".format(size // 1048576))
            return
        r = subprocess.run(["cp", "-a", "--", real, tmp], capture_output=True, text=True)
        if r.returncode != 0:
            shutil.rmtree(tmp, ignore_errors=True)
            self.fail(lf, t, "uitpakken van de map mislukt: " + r.stderr.strip()[:200])
            return
        self.rebase_inner_links(tmp, real)
        os.unlink(p)
        os.rename(tmp, p)
        self.extracted += 1
        self.walk(p, chain + (real,))

    def rebase_inner_links(self, tmp, real):
        """Een relatieve koppeling in de kopie die buiten de uitgepakte boom wees, wijst nu naar een
        andere plek dan in het origineel: zet haar om naar het absolute doel van het origineel,
        dan komt hij in de volgende stap als gewone koppeling aan de beurt."""
        for dp, entries in walk_nofollow(tmp):
            for e in entries:
                if not e.is_symlink():
                    continue
                q = e.path
                t = os.readlink(q)
                if os.path.isabs(t):
                    continue
                orig_dir = os.path.join(real, os.path.relpath(dp, tmp))
                a = os.path.normpath(os.path.join(orig_dir, t))
                if inside(a, real):
                    continue
                self.writable(dp)
                os.unlink(q)
                os.symlink(a, q)

    # --- de wandeling ----------------------------------------------------------------------------------
    def walk(self, top, chain=()):
        stack = [top]
        while stack:
            d = stack.pop()
            try:
                it = list(os.scandir(d))
            except PermissionError:
                try:
                    self.writable(os.path.dirname(d))
                    os.chmod(d, stat.S_IMODE(os.lstat(d).st_mode) | 0o500)
                    it = list(os.scandir(d))
                except OSError as e:
                    self.fail(self.final(d), "", "map onleesbaar: {}".format(e.strerror))
                    continue
            except OSError as e:
                self.fail(self.final(d), "", "map onleesbaar: {}".format(e.strerror))
                continue
            for e in it:
                if e.is_symlink():
                    self.link(e.path, chain)
                elif e.is_dir(follow_symlinks=False):
                    stack.append(e.path)

    def run(self):
        try:
            tops = []
            skip = {"User", "User.new", "User.old"}
            for n in sorted(os.listdir(self.dest)):
                if n not in skip:
                    tops.append(os.path.join(self.dest, n))
            if self.scan_user:
                tops.append(self.actual_user)
            for top in tops:
                if os.path.islink(top):
                    self.link(top)
                elif os.path.isdir(top):
                    self.walk(top)
        finally:
            self.restore_modes()
        # Een lege map mag er alleen zijn omdat hij op de toegestane lijst stond.
        print("  koppelingen: {}, herschreven: {}, uitgepakt: {}, leeg (toegestaan): {}, absoluut naar buiten "
              "(NAS e.d.): {}, fout: {}".format(self.total, self.rewritten, self.extracted, len(self.emptied),
                                                self.outside, len(self.bad)))
        for lf, t, why in self.bad[:30]:
            print("  FOUT: {} -> {} ({})".format(lf, t, why), file=sys.stderr)
        if len(self.bad) > 30:
            print("  ... en nog {} andere".format(len(self.bad) - 30), file=sys.stderr)
        return 1 if self.bad else 0


def cmd_links(a):
    return Linker(a).run()


# --- overlay-toestand --------------------------------------------------------------------------------
def overlay_xattrs(path):
    try:
        names = os.listxattr(path, follow_symlinks=False)
    except OSError:
        return {}
    out = {}
    for n in names:
        if n.startswith("user.overlay.") or n.startswith("trusted.overlay."):
            try:
                out[n] = os.getxattr(path, n, follow_symlinks=False)
            except OSError:
                pass
    return out


def is_whiteout(st):
    return stat.S_ISCHR(st.st_mode) and st.st_rdev == 0


def scan_overlay(top, in_upper=False):
    """Alle whiteouts (relatief pad -> mode) en overlay-xattrs (relatief pad -> {naam: waarde}) onder top.
    Bestanden worden alleen op xattrs bekeken onder een map met de naam 'upper' (daar zet de overlay
    redirect/metacopy), mappen altijd. Wat tijdens het scannen verdwijnt (een draaiende game) wordt overgeslagen."""
    wh, xa = {}, {}
    stack = [(top, in_upper or os.path.basename(top) == "upper")]
    while stack:
        d, up = stack.pop()
        rel = os.path.relpath(d, top)
        x = overlay_xattrs(d)
        if x:
            xa[rel] = x
        try:
            entries = list(os.scandir(d))
        except (FileNotFoundError, NotADirectoryError):
            continue    # verdwenen tijdens het scannen (een draaiende game); rsync meldt dat als code 24
        except OSError as e:
            raise SystemExit("make-portable: {} niet te lezen: {}".format(d, e.strerror))
        for e in entries:
            try:
                if e.is_symlink():
                    continue
                if e.is_dir(follow_symlinks=False):
                    stack.append((e.path, up or e.name == "upper"))
                elif e.is_file(follow_symlinks=False):
                    if up:
                        fx = overlay_xattrs(e.path)
                        if fx:
                            xa[os.path.relpath(e.path, top)] = fx
                else:
                    st = e.stat(follow_symlinks=False)
                    if is_whiteout(st):
                        wh[os.path.relpath(e.path, top)] = stat.S_IMODE(st.st_mode)
            except FileNotFoundError:
                continue    # tussen scandir en stat verdwenen
    return wh, xa


def overlay_roots(top, scope):
    """De mappen onder top (relatief) waar overlay-toestand staat. 'all': de hele boom. 'auto': een boom met de naam writelayer of
    een top met user.overlay.*-xattrs helemaal, anders alleen de mappen 'upper' tot drie niveaus diep (native/<Game>/<n>/upper);
    koppelingen worden niet gevolgd."""
    if scope == "all" or os.path.basename(top) == "writelayer" or overlay_xattrs(top):
        return ["."]
    out = []
    stack = [(top, 0)]
    while stack:
        d, depth = stack.pop()
        try:
            with os.scandir(d) as it:
                entries = list(it)
        except OSError:
            continue
        for e in entries:
            try:
                if not e.is_dir(follow_symlinks=False):
                    continue
            except OSError:
                continue
            if e.name == "upper":
                out.append(os.path.relpath(e.path, top))
            elif depth < 2:
                stack.append((e.path, depth + 1))
    return sorted(out)


def cmd_whiteouts(a):
    src, dst = a.src.rstrip("/"), a.dst.rstrip("/")
    name = os.path.basename(src)
    if not os.path.isdir(src) or os.path.islink(src):
        return 0
    lenient = a.rsync_rc == 24
    problems, notes = [], []

    def differs(msg):      # een verschil op een pad dat in beide bomen staat
        (notes if lenient else problems).append(msg)

    swh, sxa = {}, {}
    for root in overlay_roots(src, a.scope):
        wh, xa = scan_overlay(os.path.normpath(os.path.join(src, root)))
        for rel, v in wh.items():
            swh[os.path.normpath(os.path.join(root, rel))] = v
        for rel, v in xa.items():
            sxa[os.path.normpath(os.path.join(root, rel))] = v
    made = 0
    made_set = set()
    for rel, mode in sorted(swh.items()):
        q = os.path.join(dst, rel)
        try:
            st = os.lstat(q)
        except (FileNotFoundError, NotADirectoryError):
            st = None
        if st is not None:
            if not is_whiteout(st):
                differs("{}: bestaat in de kopie, maar is geen whiteout".format(rel))
            continue
        d = os.path.dirname(q)
        if not os.path.isdir(d) or os.path.islink(d):
            notes.append("{}: de map ontstond na het kopiëren in de bron; niet vergeleken".format(rel))
            continue
        old = None
        try:
            try:
                os.mknod(q, stat.S_IFCHR | mode, os.makedev(0, 0))
            except PermissionError:
                old = stat.S_IMODE(os.lstat(d).st_mode)
                os.chmod(d, old | 0o700)
                os.mknod(q, stat.S_IFCHR | mode, os.makedev(0, 0))
            made += 1
            made_set.add(rel)
        except FileNotFoundError:
            notes.append("{}: de map verdween uit de kopie; niet vergeleken".format(rel))
        except OSError as e:
            problems.append("{}: whiteout aanmaken mislukt: {}".format(rel, e.strerror))
        finally:
            if old is not None:
                try:
                    os.chmod(d, old)
                except OSError:
                    pass
    # Controle per pad (geen tweede wandeling door de kopie): wat we aanmaakten moet er als 0:0 staan.
    for rel in sorted(made_set):
        try:
            if is_whiteout(os.lstat(os.path.join(dst, rel))):
                continue
        except OSError:
            pass
        problems.append("{}: aangemaakt, maar in de kopie geen whiteout 0:0".format(rel))
    xa_ok = 0
    for rel, x in sorted(sxa.items()):
        q = os.path.normpath(os.path.join(dst, rel))
        if not os.path.lexists(q):
            notes.append("{}: staat niet in de kopie (na het kopiëren in de bron ontstaan); xattrs niet vergeleken".format(rel))
            continue
        if overlay_xattrs(q) != x:
            differs("{}: mist user.overlay.*-xattrs of heeft andere waarden".format(rel))
        else:
            xa_ok += 1
    for p in problems[:20]:
        print("  FOUT overlay {}: {}".format(name, p), file=sys.stderr)
    if len(problems) > 20:
        print("  FOUT overlay {}: ... en nog {} andere".format(name, len(problems) - 20), file=sys.stderr)
    for n in notes[:10]:
        print("  let op overlay {}: {}".format(name, n), file=sys.stderr)
    if len(notes) > 10:
        print("  let op overlay {}: ... en nog {} andere".format(name, len(notes) - 10), file=sys.stderr)
    if problems:
        return 1
    if swh or sxa:
        print("  overlay {}: {} whiteouts ({} bijgemaakt), {} xattr-dragers gelijk{}".format(
            name, len(swh), made, xa_ok,
            ", {} niet vergeleken (de bron veranderde tijdens of na het kopiëren)".format(len(notes)) if notes else ""))
    return 0


# --- in gebruik? ----------------------------------------------------------------------------------------------------
def _octal_unescape(v):
    out, i = [], 0
    while i < len(v):
        if v[i] == "\\" and i + 3 < len(v) and v[i + 1:i + 4].isdigit():
            out.append(chr(int(v[i + 1:i + 4], 8)))
            i += 4
        else:
            out.append(v[i])
            i += 1
    return "".join(out)


def _mountinfo(path):
    """[(dev, root, koppelpunt, regel)] uit een mountinfo; [] als hij niet te lezen is."""
    try:
        with open(path, encoding="utf-8", errors="surrogateescape") as f:
            lines = f.read().splitlines()
    except OSError:
        return []
    out = []
    for line in lines:
        parts = line.split(" ")
        if len(parts) >= 5:
            out.append((parts[2], _octal_unescape(parts[3]), _octal_unescape(parts[4]), line))
    return out


def _fs_relative(path):
    """(apparaat, pad binnen dat bestandssysteem) van path, via het langste eigen koppelpunt."""
    best, res = -1, (None, None)
    for dev, root, mp, _line in _mountinfo("/proc/self/mountinfo"):
        if not inside(path, mp) and mp != "/":
            continue
        if len(mp) < best:
            continue
        best = len(mp)
        rest = path if mp == "/" else path[len(mp):]
        res = (dev, (root.rstrip("/") + rest) or "/")
    return res


def _mentions(text, path):
    """path staat in text als heel pad(begin): erna een '/', een scheidingsteken of het einde (User telt niet voor User.new)."""
    i = text.find(path)
    while i >= 0:
        e = i + len(path)
        if e == len(text) or text[e] in "/ ,:\n\t":
            return True
        i = text.find(path, i + 1)
    return False


def cmd_busy(a):
    """Exit 1 als een proces dat we kunnen lezen een van de mappen gebruikt: een mount ervan of erin (ook via een bind onder een
    ander pad in een sandbox, en overlay-opties), zijn werkmap of wortel, een open bestand of een gemapt bestand. Exit 0 als niet."""
    paths = [os.path.realpath(p) for p in a.paths if os.path.lexists(p)]
    if not paths:
        return 0
    rels = [(p,) + _fs_relative(p) for p in paths]
    me = os.getpid()
    hits = []
    seen_ns = set()
    for pid in os.listdir("/proc"):
        if not pid.isdigit() or int(pid) == me:
            continue
        base = "/proc/" + pid
        why = []
        try:
            ns = os.readlink(base + "/ns/mnt")
        except OSError:
            ns = None
        if ns is None or ns not in seen_ns:
            mi = _mountinfo(base + "/mountinfo")
            if mi and ns is not None:
                seen_ns.add(ns)
            for dev, root, mp, line in mi:
                for p, pdev, prel in rels:
                    if (pdev == dev and prel not in (None, "/") and inside(root, prel)) or _mentions(line, p):
                        why.append("mount " + mp)
                        break
        for link in ("cwd", "root"):
            try:
                t = os.readlink(base + "/" + link)
            except OSError:
                continue
            if any(inside(t, p) for p in paths):
                why.append(link + " " + t)
        try:
            fds = os.listdir(base + "/fd")
        except OSError:
            fds = []
        for fd in fds:
            try:
                t = os.readlink(base + "/fd/" + fd)
            except OSError:
                continue
            if any(inside(t, p) for p in paths):
                why.append("open " + t)
                break
        try:
            with open(base + "/maps", encoding="utf-8", errors="surrogateescape") as f:
                for line in f:
                    i = line.find(" /")
                    if i >= 0 and any(inside(line[i + 1:].rstrip("\n").replace(" (deleted)", ""), p) for p in paths):
                        why.append("gemapt " + line[i + 1:].strip())
                        break
        except OSError:
            pass
        if why:
            try:
                with open(base + "/comm", encoding="utf-8", errors="replace") as f:
                    comm = f.read().strip()
            except OSError:
                comm = "?"
            hits.append("pid {} ({}): {}".format(pid, comm, "; ".join(why[:3])))
    for h in hits[:20]:
        print("  in gebruik: " + h, file=sys.stderr)
    return 1 if hits else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    l = sub.add_parser("links")
    l.add_argument("--dest", required=True)
    l.add_argument("--actual-user", required=True)
    l.add_argument("--user", required=True)
    l.add_argument("--scan-user", type=int, default=1)
    l.add_argument("--runtime", required=True)
    l.add_argument("--install", required=True)
    l.add_argument("--config", required=True)
    l.add_argument("--tui-proton", required=True)
    l.add_argument("--home-root", action="append", default=[])
    l.add_argument("--empty-ok", action="append", default=[],
                   help="doelpad (met * als joker) van een mapkoppeling die een lege map mag worden")
    l.add_argument("--max-extract", type=int, default=2 * 1024 ** 3)
    l.set_defaults(fn=cmd_links)
    w = sub.add_parser("whiteouts")
    w.add_argument("--rsync-rc", type=int, default=0,
                   help="afloop van de rsync ervoor; bij 24 (bestanden verdwenen) worden verschillen waarschuwingen")
    w.add_argument("--scope", choices=("all", "auto"), default="all",
                   help="all: de hele boom; auto: alleen writelayer, tops met overlay-xattrs en mappen 'upper' (zie boven)")
    w.add_argument("src")
    w.add_argument("dst")
    w.set_defaults(fn=cmd_whiteouts)
    b = sub.add_parser("busy")
    b.add_argument("paths", nargs="+")
    b.set_defaults(fn=cmd_busy)
    a = ap.parse_args()
    sys.exit(a.fn(a))


if __name__ == "__main__":
    main()
