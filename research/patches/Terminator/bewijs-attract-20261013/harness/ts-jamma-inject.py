#!/usr/bin/env python3
"""ts-jamma-inject.py -- press a cabinet switch on Terminator Salvation's serial "JAMMA I/O" link.

The game opens /dev/ttyS0 (115200 raw) and parses frames  LF 'S' <16 hex digits> CR.
Decoded: byte0='S', byte1=status (bit7 must be set), bytes 2..8 = fourteen 4-bit
transition counters (one per switch).  A press is counter+1, a release is counter+1 again.

Usage:  ts-jamma-inject.py PEER_PTY [switch] [--hold MS] [--state FILE] [--sync-only] [--drain-only]
  PEER_PTY  the far end of the pty the game has as /dev/ttyS0
            (bench harness: /tmp/ts-b8/log/tv/pty/ttyS0-peer)
  switch    start1 (default) start2 coin1 coin2 test service bill volup voldown
            trig1 trig2 pump1 pump2 tilt
"""
import os, sys, time, json, argparse, select

# name -> (byte index in decoded frame, shift)   [table at 0x08721e60 in the game]
SW = {
    "trig1": (2, 4), "trig2": (2, 0), "pump1": (3, 4), "pump2": (3, 0),
    "start1": (4, 4), "start2": (4, 0), "coin1": (5, 4), "coin2": (5, 0),
    "volup": (6, 4), "voldown": (6, 0), "test": (7, 4), "service": (7, 0),
    "bill": (8, 0), "tilt": (8, 4),
}

def frame(b):            # b: 7 payload bytes (decoded bytes 2..8)
    return b"\nS80" + "".join("%02X" % x for x in b).encode() + b"\r"

def drain(fd, label, quiet=False):
    got = b""
    while select.select([fd], [], [], 0.05)[0]:
        try:
            d = os.read(fd, 4096)
        except BlockingIOError:
            break
        if not d:
            break
        got += d
    if got and not quiet:
        print("%s: %d bytes from game: %r" % (label, len(got), got[-200:]))
    elif not quiet:
        print("%s: nothing from game" % label)
    return got

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pty")
    ap.add_argument("switch", nargs="?", default="start1", choices=sorted(SW))
    ap.add_argument("--hold", type=int, default=300)
    ap.add_argument("--state", default=None)
    ap.add_argument("--sync-only", action="store_true")
    ap.add_argument("--drain-only", action="store_true")
    a = ap.parse_args()
    statef = a.state or (os.path.realpath(a.pty).replace("/", "_") + ".jamma-state")
    if not a.state:
        statef = os.path.join(os.path.dirname(os.path.abspath(__file__)), statef)
    try:
        b = json.load(open(statef))
        assert len(b) == 7
    except Exception:
        b = [0] * 7
    fd = os.open(a.pty, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    drain(fd, "before")
    if a.drain_only:
        return
    def send(tag):
        f = frame(b)
        os.write(fd, f)
        json.dump(b, open(statef, "w"))
        print("%-8s %r" % (tag, f))
    send("sync")                       # first frame after game start = baseline; otherwise a no-op
    time.sleep(0.25)
    if not a.sync_only:
        i, sh = SW[a.switch]
        def bump():
            n = ((b[i - 2] >> sh) + 1) & 0xF
            b[i - 2] = (b[i - 2] & ~(0xF << sh) & 0xFF) | (n << sh)
        if (b[i - 2] >> sh) & 1:       # a previous run died while "pressed": release first
            bump(); send("release"); time.sleep(0.25)
        bump(); send("press")
        time.sleep(a.hold / 1000.0)
        bump(); send("release")
        time.sleep(0.25)
    drain(fd, "after")

if __name__ == "__main__":
    main()
