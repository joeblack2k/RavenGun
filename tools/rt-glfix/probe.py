#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# rt-glfix — meetgereedschap voor Jurassic Park (en de andere 32-bits Raw Thrills-ELF's).
#
# Waarom dit bestaat: het spel draait als 32-bits ELF in het Wine-proces, op zijn eigen
# koppeladressen (0x08048000 e.o.). Om de GOT-herschrijving te kunnen bewijzen moeten we in dat
# proces kunnen lezen én schrijven. /proc/<pid>/mem is met yama ptrace_scope=1 alleen leesbaar
# voor een voorouder van dat proces. Daarom start dit script zelf scripts/launch-direct.py en
# is het dus de grootouder van het wine-proces.
#
# Gebruik:
#   tools/rt-glfix/probe.py --profile JurassicPark --read 0x8add63c:32
#   tools/rt-glfix/probe.py --profile JurassicPark --write 0x8add63c=0x08048000
#   tools/rt-glfix/probe.py --profile JurassicPark --env LD_PRELOAD=/pad/librt_glfix.so
#
# Opties:
#   --profile NAAM     spelprofiel (verplicht)
#   --env K=V          extra omgeving voor launch-direct.py (herhaalbaar)
#   --read ADDR:LEN    lezen en tonen (herhaalbaar)
#   --write ADDR=VAL   schrijven (herhaalbaar); gebeurt na de eerste meting
#   --scan ADDR:LEN    als dword-lijst tonen (herhaalbaar)
#   --keep SEC         na de metingen nog zoveel seconden laten lopen (standaard 8)
#   --shot PAD         schermafdruk maken met ffmpeg x11grab
#   --secs N           totale looptijd voordat we afbreken (standaard 60)
#   --raw              de volledige uitvoer van launch-direct.py op het scherm
import argparse
import os
import re
import signal
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LAUNCH = os.path.join(ROOT, "scripts", "launch-direct.py")
HOME = os.environ.get("HOME", "/home/deck")
XAUTH = os.environ.get("RG_XAUTH", "/run/user/1000/xauth_NBglcp")
DISPLAY = os.environ.get("RG_DISPLAY", ":0")


def log(m):
    print("[probe] " + m, flush=True)


def wine_pid():
    """Het wine-proces dat BudgieLoader.exe draait (het spel zit in hetzelfde adresbeeld)."""
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        try:
            with open("/proc/%s/cmdline" % d, "rb") as f:
                c = f.read().replace(b"\0", b" ").decode("utf-8", "replace")
        except OSError:
            continue
        if "BudgieLoader.exe" in c and "probe.py" not in c and "launch-direct" not in c:
            return int(d)
    return None


def open_mem(pid):
    return os.open("/proc/%d/mem" % pid, os.O_RDWR)


def rd(fd, addr, n):
    try:
        return os.pread(fd, n, addr)
    except OSError as e:
        return "leesfout: %s" % e


def wr(fd, addr, data):
    try:
        return os.pwrite(fd, data, addr)
    except OSError as e:
        return "schrijffout: %s" % e


def dump_maps(pid, pattern=None):
    out = []
    for line in open("/proc/%d/maps" % pid):
        if pattern is None or re.search(pattern, line):
            out.append(line.rstrip())
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--profile", required=True)
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--read", action="append", default=[])
    ap.add_argument("--write", action="append", default=[])
    ap.add_argument("--scan", action="append", default=[])
    ap.add_argument("--maps", default=None)
    ap.add_argument("--keep", type=float, default=8.0)
    ap.add_argument("--shot", default=None)
    ap.add_argument("--secs", type=float, default=60.0)
    ap.add_argument("--raw", action="store_true")
    a = ap.parse_args()

    env = dict(os.environ)
    env["XAUTHORITY"] = XAUTH
    env["DISPLAY"] = DISPLAY
    cmd = [sys.executable, LAUNCH, a.profile, "--run"]
    for e in a.env:
        cmd += ["--env", e]
    log("start: " + " ".join(cmd))
    out = open("/tmp/rt-glfix-launch.log", "wb")
    p = subprocess.Popen(cmd, cwd=ROOT, env=env, stdout=out, stderr=subprocess.STDOUT)

    def tail():
        try:
            return open("/tmp/rt-glfix-launch.log", "rb").read().decode("utf-8", "replace")
        except OSError:
            return ""

    t0 = time.time()
    pid = None
    while time.time() - t0 < a.secs:
        if p.poll() is not None:
            break
        pid = wine_pid()
        if pid and "Starting Game" in tail():
            break
        time.sleep(0.25)

    result = {"profile": a.profile, "pid": pid, "reads": {}, "writes": {}, "scans": {}, "exit": None}
    if pid is None:
        log("geen wine-proces gevonden")
    else:
        log("wine-proces pid %d" % pid)
        time.sleep(1.0)
        if a.maps:
            print("--- kaart (patroon %s) ---" % a.maps)
            for l in dump_maps(pid, a.maps):
                print(l)
        try:
            fd = open_mem(pid)
        except OSError as e:
            log("kan /proc/%d/mem niet openen: %s" % (pid, e))
            fd = None
        if fd is not None:
            for spec in a.read:
                addr, ln = spec.split(":")
                addr, ln = int(addr, 0), int(ln, 0)
                v = rd(fd, addr, ln)
                if isinstance(v, bytes):
                    print("lees 0x%08x (%d): %s" % (addr, ln, v.hex(" ")))
                    result["reads"][hex(addr)] = v.hex()
                else:
                    print("lees 0x%08x: %s" % (addr, v))
            for spec in a.scan:
                addr, ln = spec.split(":")
                addr, ln = int(addr, 0), int(ln, 0)
                v = rd(fd, addr, ln)
                if isinstance(v, bytes):
                    dwords = [int.from_bytes(v[i:i + 4], "little") for i in range(0, len(v) - 3, 4)]
                    print("scan 0x%08x (%d dwords): %s" % (addr, len(dwords), " ".join("%08x" % d for d in dwords)))
                    result["scans"][hex(addr)] = ["%08x" % d for d in dwords]
            for spec in a.write:
                addr, val = spec.split("=")
                addr, val = int(addr, 0), int(val, 0)
                n = wr(fd, addr, val.to_bytes(4, "little"))
                print("schrijf 0x%08x = 0x%08x -> %s" % (addr, val, n))
                result["writes"][hex(addr)] = hex(val)
                v = rd(fd, addr, 4)
                if isinstance(v, bytes):
                    print("  teruggelezen: %s" % v.hex(" "))
            os.close(fd)

    if a.keep > 0:
        time.sleep(a.keep)
    if a.shot:
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "x11grab",
                        "-video_size", "1920x1080", "-i", DISPLAY, "-frames:v", "1", a.shot],
                       env=env, check=False)
        log("schermafdruk: %s" % a.shot)

    # nog even laten lopen tot het spel zelf stopt, met een bovengrens
    while p.poll() is None and time.time() - t0 < a.secs:
        time.sleep(0.5)
    if p.poll() is None:
        log("afbreken na %.0f s" % a.secs)
        p.send_signal(signal.SIGTERM)
        try:
            p.wait(timeout=15)
        except subprocess.TimeoutExpired:
            p.kill()
    result["exit"] = p.returncode
    log("launch-direct exitcode %s" % p.returncode)
    log("logbestand: /tmp/rt-glfix-launch.log")
    if a.raw:
        print(tail())
    else:
        for line in tail().splitlines():
            if line.startswith("[launch-direct") or "Warning" in line or "Detected Linux game" in line \
                    or "Starting Game" in line or "Uncaught" in line or "lockup" in line.lower():
                print("  | " + line)
    print("RESULT " + repr(result))


if __name__ == "__main__":
    main()
