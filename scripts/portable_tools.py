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
          - relatief: moet binnen de map blijven;
          - absoluut naar buiten de thuismap-indeling (de NAS): blijft staan, zonder het doel aan te raken
            (alleen lexicale controle; een realpath zou per koppeling een netwerkaanroep zijn).
          Gescand wordt alleen de boom die gebouwd wordt (bin/, lib/, share/ en eventueel de nieuwe User);
          een oude User/, User.new of User.old blijft ongemoeid. Exit 1 bij elke overtreding.
whiteouts overlay-toestand na een rsync -aX naar de kopie brengen. Een rsync zonder rootrechten slaat
          char-devices over, en dat zijn precies de whiteouts van de schrijflagen (kernel-overlay in een
          user-namespace via bwrap --overlay: whiteout = char-device 0:0, mappen en bestanden dragen
          user.overlay.*-xattrs). Maakt de ontbrekende whiteouts aan (mknod 0:0 mag zonder rechten) en
          controleert dat aantallen en user.overlay.*-waarden gelijk zijn; exit 1 als dat niet lukt.
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


def tree_size(path):
    total = 0
    for dp, dns, fns in os.walk(path):
        for n in fns:
            try:
                total += os.lstat(os.path.join(dp, n)).st_size
            except OSError:
                pass
    return total


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
            r = subprocess.run(["cp", "-aL", "--", real, tmp], capture_output=True, text=True)
            if r.returncode != 0:
                self.fail(lf, t, "uitpakken mislukt: " + r.stderr.strip()[:200])
                return
            os.replace(tmp, p)
            self.extracted += 1
            return
        # een map
        if any(fnmatch.fnmatchcase(x, pat) for pat in self.empty_ok for x in (nt, real)):
            os.unlink(p)
            os.mkdir(p)
            self.emptied.append(lf)
            print("  cache-koppeling (toegestaan) vervangen door lege map: {} (was {} MB)".format(
                lf, tree_size(real) // 1048576))
            return
        if real in chain or len(chain) >= 8:
            self.fail(lf, t, "mapkoppeling die in zichzelf uitkomt of te diep geneste mapkoppelingen")
            return
        size = tree_size(real)
        if size > self.max_extract:
            self.fail(lf, t, "map van {} MB is te groot om uit te pakken (grens {} MB); zet hem op de "
                      "toegestane lijst (RG_PORTABLE_EMPTY_OK) als de eigenaar hem zelf weer vult, of los "
                      "de koppeling op".format(size // 1048576, self.max_extract // 1048576))
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
        for dp, dns, fns in os.walk(tmp):
            for n in dns + fns:
                q = os.path.join(dp, n)
                if not os.path.islink(q):
                    continue
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


def scan_overlay(top):
    """Alle whiteouts (relatief pad -> mode) en overlay-xattrs (relatief pad -> {naam: waarde}) onder top.
    Bestanden worden alleen op xattrs bekeken onder een map met de naam 'upper' (daar zet de overlay
    redirect/metacopy), mappen altijd."""
    wh, xa = {}, {}
    stack = [(top, False)]
    while stack:
        d, in_upper = stack.pop()
        rel = os.path.relpath(d, top)
        x = overlay_xattrs(d)
        if x:
            xa[rel] = x
        try:
            entries = list(os.scandir(d))
        except FileNotFoundError:
            continue    # verdwenen tijdens het scannen (een draaiende game); rsync meldt dat als code 24
        except OSError as e:
            raise SystemExit("make-portable: {} niet te lezen: {}".format(d, e.strerror))
        for e in entries:
            if e.is_symlink():
                continue
            if e.is_dir(follow_symlinks=False):
                stack.append((e.path, in_upper or e.name == "upper"))
            elif e.is_file(follow_symlinks=False):
                if in_upper:
                    fx = overlay_xattrs(e.path)
                    if fx:
                        xa[os.path.relpath(e.path, top)] = fx
            else:
                st = e.stat(follow_symlinks=False)
                if is_whiteout(st):
                    wh[os.path.relpath(e.path, top)] = stat.S_IMODE(st.st_mode)
    return wh, xa


def cmd_whiteouts(a):
    src, dst = a.src.rstrip("/"), a.dst.rstrip("/")
    if not os.path.isdir(src) or os.path.islink(src):
        return 0
    swh, sxa = scan_overlay(src)
    made = 0
    problems = []
    for rel, mode in sorted(swh.items()):
        q = os.path.join(dst, rel)
        try:
            st = os.lstat(q)
            if is_whiteout(st):
                continue
            problems.append("{}: bestaat in de kopie, maar is geen whiteout".format(rel))
            continue
        except FileNotFoundError:
            pass
        d = os.path.dirname(q)
        old = None
        try:
            try:
                os.mknod(q, stat.S_IFCHR | mode, os.makedev(0, 0))
            except PermissionError:
                if not os.path.isdir(d):
                    raise
                old = stat.S_IMODE(os.lstat(d).st_mode)
                os.chmod(d, old | 0o700)
                os.mknod(q, stat.S_IFCHR | mode, os.makedev(0, 0))
            made += 1
        except OSError as e:
            problems.append("{}: whiteout aanmaken mislukt: {}".format(rel, e.strerror))
        finally:
            if old is not None:
                os.chmod(d, old)
    dwh, dxa = scan_overlay(dst) if os.path.isdir(dst) else ({}, {})
    if len(dwh) != len(swh):
        problems.append("aantal whiteouts: bron {}, kopie {}".format(len(swh), len(dwh)))
    missing = [r for r, x in sxa.items() if dxa.get(r) != x]
    if missing:
        problems.append("{} map(pen)/bestand(en) missen user.overlay.*-xattrs of hebben andere waarden, "
                        "bijvoorbeeld {}".format(len(missing), missing[0]))
    if problems:
        for p in problems[:20]:
            print("  FOUT overlay {}: {}".format(os.path.basename(src), p), file=sys.stderr)
        return 1
    if swh or sxa:
        print("  overlay {}: {} whiteouts ({} bijgemaakt), {} xattr-dragers gelijk".format(
            os.path.basename(src), len(swh), made, len(sxa)))
    return 0


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
    w.add_argument("src")
    w.add_argument("dst")
    w.set_defaults(fn=cmd_whiteouts)
    a = ap.parse_args()
    sys.exit(a.fn(a))


if __name__ == "__main__":
    main()
