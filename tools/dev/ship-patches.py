#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Kopieert uit de patches-repo ALLEEN de levering (wat de lader, de daemon en de app lezen) naar een doelmap.

Dezelfde lijst als de install(DIRECTORY patches ...)-regel in CMakeLists.txt en de controle in tests/install_test.sh:
<Game>/*.ini, native.json, unity.json, options.json, dongle.json, README.md, en alles onder <Game>/dropin/, data/ en seed/.
Alles daarbuiten (logboeken, metingen, foto's, notities, meetscripts) is dossier en gaat niet mee. Lege mappen ontstaan niet.

Gebruik: ship-patches.py BRON DOEL      (BRON = de patches-map, een koppeling mag; DOEL wordt aangemaakt)
"""
import os
import re
import shutil
import sys

LEVERING = re.compile(r"^[^/]+/(?:[^/]+\.ini|native\.json|unity\.json|options\.json|dongle\.json|README\.md)$|^[^/]+/(?:dropin|data|seed)/")


def ship(src, dst):
    src = os.path.realpath(src)
    n = 0
    for dp, dns, fns in os.walk(src):
        dns.sort()
        for f in sorted(fns):
            p = os.path.join(dp, f)
            rel = os.path.relpath(p, src)
            if not LEVERING.match(rel) or not os.path.exists(p):
                continue
            t = os.path.join(dst, rel)
            os.makedirs(os.path.dirname(t), exist_ok=True)
            shutil.copyfile(p, t)
            os.chmod(t, 0o755 if os.access(p, os.X_OK) else 0o644)
            n += 1
    return n


if __name__ == "__main__":
    if len(sys.argv) != 3 or not os.path.isdir(sys.argv[1]):
        sys.exit(__doc__)
    n = ship(sys.argv[1], sys.argv[2])
    if n == 0:
        sys.exit("geen enkel bestand in de levering gevonden in " + sys.argv[1])
    print("%d patchbestanden naar %s" % (n, sys.argv[2]))
