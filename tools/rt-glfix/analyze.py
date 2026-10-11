#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# rt-glfix — GOT-inventaris van een 32-bits Raw Thrills-ELF onder ElfLdr2/BudgieLoader.
#
# Dit script start scripts/launch-direct.py als kind (zo zijn we voorouder van het wine-proces en
# mogen we /proc/<pid>/mem lezen, ook met yama ptrace_scope=1), wacht tot het spel geladen is en
# doet dan drie dingen:
#   1. de PLT-stubs van het spel opzoeken: `ff 25 <adres> 68 ef be ad de` (jmp [got]; push 0xdeadbeef),
#   2. van elke GOT-invoer de waarde lezen en bepalen in welke module die valt,
#   3. de waarde via de exporttabel van die module aan een naam koppelen.
#
# Gebruik: tools/rt-glfix/analyze.py --profile JurassicPark [--json uit.json] [--env K=V]
import argparse
import json
import os
import re
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LAUNCH = os.path.join(ROOT, "scripts", "launch-direct.py")
XAUTH = os.environ.get("RG_XAUTH", "/run/user/1000/xauth_NBglcp")
DISPLAY = os.environ.get("RG_DISPLAY", ":0")

PLT_MAGIC = b"\xff\x25"


def find_wine_pid():
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        try:
            c = open("/proc/%s/cmdline" % d, "rb").read().replace(b"\0", b" ").decode("utf-8", "replace")
        except OSError:
            continue
        if "BudgieLoader.exe" in c and "analyze.py" not in c and "launch-direct" not in c:
            return int(d)
    return None


# ---------------------------------------------------------------- PE-exporttabel

def pe_exports(path):
    """{naam: rva} uit een PE-bestand op schijf."""
    try:
        data = open(path, "rb").read()
    except OSError:
        return {}
    try:
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe:pe + 4] != b"PE\0\0":
            return {}
        nsec = struct.unpack_from("<H", data, pe + 6)[0]
        opt = pe + 24
        magic = struct.unpack_from("<H", data, opt)[0]
        ddoff = opt + (96 if magic == 0x10B else 112)
        exp_rva, _ = struct.unpack_from("<II", data, ddoff)
        if not exp_rva:
            return {}
        secs = []
        so = opt + struct.unpack_from("<H", data, pe + 20)[0]
        for i in range(nsec):
            o = so + i * 40
            vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, o + 8)
            secs.append((vaddr, vsize, raddr, rsize))

        def rva2off(r):
            for vaddr, vsize, raddr, rsize in secs:
                if vaddr <= r < vaddr + max(vsize, rsize):
                    return raddr + (r - vaddr)
            return None

        eo = rva2off(exp_rva)
        if eo is None:
            return {}
        nnam = struct.unpack_from("<I", data, eo + 24)[0]
        addr_fun = struct.unpack_from("<I", data, eo + 28)[0]
        addr_nam = struct.unpack_from("<I", data, eo + 32)[0]
        addr_ord = struct.unpack_from("<I", data, eo + 36)[0]
        out = {}
        for i in range(nnam):
            nr = struct.unpack_from("<I", data, rva2off(addr_nam) + i * 4)[0]
            off = rva2off(nr)
            nm = data[off:data.index(b"\0", off)].decode("ascii", "replace")
            ordi = struct.unpack_from("<H", data, rva2off(addr_ord) + i * 2)[0]
            frva = struct.unpack_from("<I", data, rva2off(addr_fun) + ordi * 4)[0]
            out[nm] = frva
        return out
    except Exception:
        return {}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--profile", required=True)
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--json", default=None)
    ap.add_argument("--secs", type=float, default=90.0)
    ap.add_argument("--keep", type=float, default=0.0)
    a = ap.parse_args()

    env = dict(os.environ, XAUTHORITY=XAUTH, DISPLAY=DISPLAY)
    cmd = [sys.executable, LAUNCH, a.profile, "--run"]
    for e in a.env:
        cmd += ["--env", e]
    logf = "/tmp/rt-glfix-analyze.log"
    p = subprocess.Popen(cmd, cwd=ROOT, env=env, stdout=open(logf, "wb"), stderr=subprocess.STDOUT)
    t0 = time.time()
    pid = None
    while time.time() - t0 < a.secs:
        pid = find_wine_pid()
        if pid:
            txt = open(logf, errors="replace").read()
            if "Starting Game" in txt:
                break
        if p.poll() is not None:
            break
        time.sleep(0.2)
    if not pid:
        print("geen wine-proces gevonden")
        return 1
    time.sleep(0.5)
    print("pid =", pid)

    maps = []
    for line in open("/proc/%d/maps" % pid):
        m = re.match(r"([0-9a-f]+)-([0-9a-f]+) (\S{4}) (\S+)\s+\S+\s+\S+\s*(.*)", line.rstrip("\n"))
        if m:
            maps.append({"lo": int(m.group(1), 16), "hi": int(m.group(2), 16), "perm": m.group(3),
                         "path": m.group(5).strip()})
    mem = os.open("/proc/%d/mem" % pid, os.O_RDONLY)

    def rd(addr, n):
        return os.pread(mem, n, addr)

    # De spel-ELF: de anonieme rwxp-regio die op 0x08048000 begint.
    game = None
    for r in maps:
        if r["lo"] == 0x08048000 and r["perm"].startswith("rwx"):
            game = r
    if game is None:
        for r in maps:
            if r["lo"] == 0x08048000:
                game = r
    if game is None:
        print("spelbeeld niet gevonden op 0x08048000")
        return 1
    size = game["hi"] - game["lo"]
    print("spelbeeld 0x%08x-0x%08x (%d bytes), rechten %s" % (game["lo"], game["hi"], size, game["perm"]))
    img = rd(game["lo"], size)
    if len(img) != size:
        print("kon het spelbeeld niet volledig lezen (%d van %d)" % (len(img), size))
        return 1

    # Module-bases: het laagste adres per pad.
    mods = {}
    for r in maps:
        if r["path"].startswith("/") or r["path"].startswith("Z:"):
            mods.setdefault(r["path"], []).append(r)
    base = {}
    for path, rs in mods.items():
        base[path] = (min(x["lo"] for x in rs), max(x["hi"] for x in rs))
    exports = {}
    for path in base:
        if path.lower().endswith((".dll", ".exe")):
            exports[path] = pe_exports(path)
    print("modules met exporttabel:", ", ".join(sorted(os.path.basename(p) for p in exports)))

    def classify(v):
        for path, (lo, hi) in base.items():
            if lo <= v < hi:
                return path
        return None

    # PLT-stubs zoeken.
    slots = {}
    i = 0
    while True:
        i = img.find(PLT_MAGIC, i)
        if i < 0:
            break
        if i + 11 <= size and img[i + 6:i + 11] == b"\x68\xef\xbe\xad\xde":
            addr = struct.unpack_from("<I", img, i + 2)[0]
            slots.setdefault(addr, []).append(game["lo"] + i)
        i += 1
    print("PLT-stubs gevonden: %d, unieke GOT-invoeren: %d" % (sum(len(v) for v in slots.values()), len(slots)))

    per_module = {}
    rows = []
    for addr in sorted(slots):
        v = struct.unpack("<I", rd(addr, 4))[0]
        path = classify(v)
        name = ""
        if path and path in exports:
            nm = [n for n, r in exports[path].items() if base[path][0] + r == v]
            name = nm[0] if len(nm) == 1 else ("(%d kandidaten)" % len(nm) if nm else "")
        per_module[os.path.basename(path) if path else "NIET IN EEN MODULE"] = \
            per_module.get(os.path.basename(path) if path else "NIET IN EEN MODULE", 0) + 1
        rows.append({"slot": "0x%08x" % addr, "waarde": "0x%08x" % v,
                     "module": os.path.basename(path) if path else None, "naam": name,
                     "stubs": ["0x%08x" % s for s in slots[addr]]})
    print()
    print("verdeling per module:")
    for k, n in sorted(per_module.items(), key=lambda kv: -kv[1]):
        print("  %-30s %d" % (k, n))
    print()
    print("GL-invoeren (naam begint met gl):")
    gl = [r for r in rows if r["naam"].startswith("gl")]
    for r in gl[:120]:
        print("  %s -> %s  %s" % (r["slot"], r["waarde"], r["naam"]))
    print("  totaal %d" % len(gl))
    print()
    for want in ("dlsym", "dlopen", "glXGetProcAddress", "glXGetProcAddressARB", "SDL_Init"):
        hit = [r for r in rows if r["naam"] == want]
        print("  %-22s %s" % (want, hit if hit else "(geen GOT-invoer gevonden)"))
    print()
    print("invoeren zonder module (waarde valt buiten elke mapping):")
    for r in rows:
        if r["module"] is None:
            print("  %s -> %s" % (r["slot"], r["waarde"]))

    if a.json:
        json.dump({"pid": pid, "game": "0x%08x-0x%08x" % (game["lo"], game["hi"]),
                   "modules": {os.path.basename(k): ["0x%08x" % v[0], "0x%08x" % v[1]] for k, v in base.items()},
                   "slots": rows}, open(a.json, "w"), indent=1)
        print("json: " + a.json)

    os.close(mem)
    if a.keep:
        time.sleep(a.keep)
    p.terminate()
    try:
        p.wait(timeout=20)
    except subprocess.TimeoutExpired:
        p.kill()
    return 0


if __name__ == "__main__":
    sys.exit(main())
