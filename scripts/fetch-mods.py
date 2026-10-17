#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Haalt de mods op die een game in zijn game.ini beschrijft ([Mod <naam>], docs/per-game-patches.md, "Mods"), bv. de Engelse
# fanvertaling van Mario Kart Arcade GP DX. Een mod komt als tar.gz van een vastgepinde commit; alleen de map Root wordt uitgepakt,
# naar <data>/mods/<Game>/<naam>/files, en gecontroleerd met TreeSha256: de sha256 van de regels "sha256  ./pad" van alle bestanden,
# op pad gesorteerd (zoals `find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum | sha256sum`). Niets ervan komt in git.
# Een mod die al met de juiste boom klaarstaat wordt overgeslagen.
#
#   scripts/fetch-mods.py [--patches DIR] [GAME ...]     zonder GAME: alle games met een [Mod ...] (zoekvolgorde van gamesettings.py)
import argparse
import hashlib
import os
import shutil
import sys
import tarfile
import tempfile
import urllib.request

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python3 -I neemt de scriptmap niet mee
import gamesettings as gs  # noqa: E402  (de ene zoekvolgorde van patches/<Game>/)


def tree_sha256(root):
    paden = []
    for d, _, files in os.walk(root):
        for f in files:
            paden.append("./" + os.path.relpath(os.path.join(d, f), root))
    regels = []
    for rel in sorted(paden, key=lambda s: s.encode()):
        h = hashlib.sha256()
        with open(os.path.join(root, rel), "rb") as fh:
            for blok in iter(lambda: fh.read(1 << 20), b""):
                h.update(blok)
        regels.append(f"{h.hexdigest()}  {rel}\n")
    return hashlib.sha256("".join(regels).encode()).hexdigest()


def mods_of(game, shipped=None):
    """[Mod <naam>] uit de samengevoegde game.ini (meegeleverd, dan de gebruikerslaag)."""
    g = gs.load_game(game, shipped=shipped)
    for sect, kv in g.settings.items():
        if sect.startswith("Mod ") and sect[4:].strip():
            yield sect[4:].strip(), kv


def fetch(game, name, m, data):
    for k in ("Url", "Root", "TreeSha256"):
        if not m.get(k):
            print(f"fetch-mods: {game} [Mod {name}] mist {k}", file=sys.stderr)
            return False
    dest = os.path.join(data, "mods", game, name)
    stamp = os.path.join(dest, "tree-sha256")
    want = m["TreeSha256"].strip().lower()
    if os.path.isfile(stamp) and open(stamp).read().strip() == want and os.path.isdir(os.path.join(dest, "files")):
        print(f"fetch-mods: {game}/{name} staat al klaar in {dest}/files")
        return True
    os.makedirs(dest, exist_ok=True)
    root = m["Root"].strip().strip("/")
    with tempfile.TemporaryDirectory(dir=dest, prefix=".part-") as tmp:
        tgz = os.path.join(tmp, "mod.tar.gz")
        print(f"fetch-mods: download {m['Url']}")
        with urllib.request.urlopen(m["Url"], timeout=60) as r, open(tgz, "wb") as out:
            shutil.copyfileobj(r, out, 1 << 20)
        uit = os.path.join(tmp, "uit")
        with tarfile.open(tgz) as tf:
            # GitHub zet alles onder <repo>-<commit>/; alleen <top>/<Root>/ uitpakken, met het data-filter (geen links naar buiten).
            leden = [t for t in tf.getmembers() if "/" in t.name and t.name.split("/", 1)[1].startswith(root + "/")]
            if not leden:
                print(f"fetch-mods: {game}/{name}: de map {root} staat niet in het archief", file=sys.stderr)
                return False
            tf.extractall(uit, members=leden, filter="data")
        top = os.path.join(uit, leden[0].name.split("/", 1)[0], root)
        have = tree_sha256(top)
        if have != want:
            print(f"fetch-mods: {game}/{name}: TreeSha256 klopt niet ({have}, verwacht {want})", file=sys.stderr)
            return False
        files = os.path.join(dest, "files")
        shutil.rmtree(files, ignore_errors=True)
        os.rename(top, files)
    with open(stamp, "w") as fh:
        fh.write(want + "\n")
    print(f"fetch-mods: {game}/{name} uitgepakt in {files}")
    return True


def main():
    ap = argparse.ArgumentParser(description="Haalt de mods uit [Mod <naam>] van game.ini op.")
    ap.add_argument("--patches", help="een meegeleverde patches-map (standaard de zoekvolgorde van gamesettings.py)")
    ap.add_argument("games", nargs="*")
    a = ap.parse_args()
    data = os.path.join(os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share"), "ravengun")
    ok = True
    for game in a.games or gs.list_games(shipped=a.patches):
        for name, m in mods_of(game, a.patches):
            ok = fetch(game, name, m, data) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
